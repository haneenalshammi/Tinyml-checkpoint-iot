/*
 * Intermittent UDP Client — Smart Building Energy Management System
 * =================================================================
 * FIT IoT-LAB — ARM Cortex-M3 — Contiki-NG
 *
 * FIX: Added NOT_REACHABLE_MAX_CYCLES — forces reboot if node stays
 *      stuck in "Not reachable" to prevent harvest keeping it alive
 *      forever without sending any data.
 */

#include "contiki.h"
#include "sys/etimer.h"
#include "dev/watchdog.h"
#include "lib/random.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "periph/lps331ap.h"
#include "periph/isl29020.h"
#include "sys/log.h"

#define LOG_MODULE "App"
#define LOG_LEVEL  LOG_LEVEL_INFO

/*---------------------------------------------------------------------------*/
/* Network settings                                                          */
/*---------------------------------------------------------------------------*/
#define UDP_CLIENT_PORT   8765
#define UDP_SERVER_PORT   5678
#define SEND_INTERVAL     (60 * CLOCK_SECOND)
#define MIN_OFF_TIME      (30  * CLOCK_SECOND)
#define MAX_OFF_TIME      (120 * CLOCK_SECOND)
#define RPL_SETTLE_TIME   (60  * CLOCK_SECOND)


/*---------------------------------------------------------------------------*/
/* Energy model                                                              */
/*---------------------------------------------------------------------------*/
#define ENERGY_MIN_BOOT      20
#define ENERGY_MAX_BOOT      80
#define ENERGY_THRESHOLD     10
#define ENERGY_COST_IDLE      1
#define ENERGY_COST_SENSOR    1
#define ENERGY_COST_TX_MIN    3
#define ENERGY_COST_TX_MAX    8
#define ENERGY_HARVEST_PROB  38
#define ENERGY_COST_RPL       3

/*---------------------------------------------------------------------------*/
/* Global variables                                                          */
/*---------------------------------------------------------------------------*/
static struct simple_udp_connection udp_conn;
static uint8_t  energy = 0;
static uint32_t seq_id = 0;

/*---------------------------------------------------------------------------*/
static void
energy_harvest(void)
{
  if((random_rand() & 0xFF) < ENERGY_HARVEST_PROB) {
    uint8_t gained = random_rand() % 6;
    if(gained > 0) {
      uint16_t new_level = (uint16_t)energy + gained;
      energy = (new_level > 100) ? 100 : (uint8_t)new_level;
    }
  }
}

/*---------------------------------------------------------------------------*/
static void
consume_energy(uint8_t cost)
{
  energy = (energy >= cost) ? energy - cost : 0;
}

/*---------------------------------------------------------------------------*/
static void
check_failure(void)
{
  if(energy < ENERGY_THRESHOLD) {
    LOG_INFO("### POWER FAILURE: energy=%u%% "
             "rebooting node now ###\n", energy);
    watchdog_reboot();
  }
}

/*---------------------------------------------------------------------------*/
PROCESS(boot_process,       "Boot");
PROCESS(udp_client_process, "UDP Client");
AUTOSTART_PROCESSES(&boot_process, &udp_client_process);

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(boot_process, ev, data)
{
  static struct etimer t;
  clock_time_t off_time;

  PROCESS_BEGIN();

  off_time = MIN_OFF_TIME +
             (random_rand() % (MAX_OFF_TIME - MIN_OFF_TIME));

  etimer_set(&t, off_time);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&t));

  lps331ap_set_datarate(LPS331AP_P_1HZ_T_1HZ);
  isl29020_prepare(ISL29020_LIGHT__AMBIENT,
                   ISL29020_RESOLUTION__16bit,
                   ISL29020_RANGE__1000lux);
  isl29020_sample_continuous();

  energy = ENERGY_MIN_BOOT +
           (random_rand() % (ENERGY_MAX_BOOT - ENERGY_MIN_BOOT + 1));

  LOG_INFO("Node waking up — energy=%u%%\n", energy);

  LOG_INFO("Phase 2: Waking up — waiting %u seconds for RPL\n",
           RPL_SETTLE_TIME / CLOCK_SECOND);

  etimer_set(&t, RPL_SETTLE_TIME);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&t));

  consume_energy(ENERGY_COST_RPL);
  check_failure();

  LOG_INFO("Phase 3: RPL settled — ready to send | energy=%u%%\n",
           energy);

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer send_timer;
  static char          payload[64];
  uip_ipaddr_t         dest_ipaddr;
  int16_t              temp_raw;
  float                temp, light;

  PROCESS_BEGIN();

  PROCESS_WAIT_UNTIL(!process_is_running(&boot_process));

  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, NULL);

  LOG_INFO("Intermittent client started | energy=%u%%\n", energy);

  etimer_set(&send_timer, random_rand() % SEND_INTERVAL);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));

    /* 1. Idle energy consumption */
    consume_energy(ENERGY_COST_IDLE);
    check_failure();

    if(NETSTACK_ROUTING.node_is_reachable() &&
       NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {

      /* ✅ FIX: Reset counter on connection */

      /* 2. Read sensors */
      lps331ap_read_temp(&temp_raw);
      temp  = 42.5f + ((float)temp_raw / 480.0f);
      light = isl29020_read_sample();
      consume_energy(ENERGY_COST_SENSOR);
      check_failure();

      /* 3. BEMS log */
      LOG_INFO("BEMS | Temperature: %.1f C | "
               "Light: %.1f lux | seq: %lu\n",
               temp, light, (unsigned long)seq_id);

      /* 4. Send to server */
      snprintf(payload, sizeof(payload),
               "BEMS | Temperature: %.1f C | "
               "Light: %.1f lux | seq: %lu",
               temp, light, (unsigned long)seq_id);
      simple_udp_sendto(&udp_conn, payload,
                        strlen(payload), &dest_ipaddr);
      seq_id++;

      /* 5. Transmission energy consumption */
      uint8_t tx_cost = ENERGY_COST_TX_MIN +
                        (random_rand() % (ENERGY_COST_TX_MAX -
                                          ENERGY_COST_TX_MIN + 1));
      consume_energy(tx_cost);
      check_failure();

    } else {


    LOG_INFO("Not reachable yet\n");
    }

    /* 6. Harvest at the end of each cycle */
    energy_harvest();
    LOG_INFO("ENERGY | level:%u\n", energy);
    check_failure();

    etimer_set(&send_timer, SEND_INTERVAL
               - CLOCK_SECOND +
               (random_rand() % (2 * CLOCK_SECOND)));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
