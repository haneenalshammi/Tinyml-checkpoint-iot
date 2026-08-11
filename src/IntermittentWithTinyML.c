/*
 * Intermittent UDP Client + ML Checkpoint
 * ========================================
 * Smart Building Energy Management System (BEMS)
 * FIT IoT-LAB — ARM Cortex-M3 — Contiki-NG
 *
 * Same intermittent code + Checkpoint + ML
 */

#include "contiki.h"
#include "sys/etimer.h"
#include "dev/watchdog.h"
#include "dev/xmem.h"       /*  Addition: NOR Flash */
#include "lib/random.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "models.h"         /*  Addition: ML model */
#include "periph/lps331ap.h"
#include "periph/isl29020.h"
#include "sys/log.h"
#include <string.h>

#define LOG_MODULE "App"
#define LOG_LEVEL  LOG_LEVEL_INFO

/*---------------------------------------------------------------------------*/
/* Network settings — same as original                                       */
/*---------------------------------------------------------------------------*/
#define UDP_CLIENT_PORT      8765
#define UDP_SERVER_PORT      5678
#define SEND_INTERVAL        (60 * CLOCK_SECOND)
#define MIN_OFF_TIME         (30  * CLOCK_SECOND)
#define MAX_OFF_TIME         (120 * CLOCK_SECOND)
#define RPL_SETTLE_TIME      (60  * CLOCK_SECOND)

/*---------------------------------------------------------------------------*/
/* Energy model — same as original                                           */
/*---------------------------------------------------------------------------*/
#define ENERGY_MIN_BOOT      20
#define ENERGY_MAX_BOOT      80
#define ENERGY_THRESHOLD     10
#define ENERGY_COST_IDLE      1
#define ENERGY_COST_SENSOR    1
#define ENERGY_COST_TX_MIN    3
#define ENERGY_COST_TX_MAX    8
#define ENERGY_HARVEST_PROB  38   /* FIX 3: was 38, correct = 85 */
#define ENERGY_COST_RPL       3
#define ENERGY_COST_ML         1
#define ENERGY_COST_CHECKPOINT 5
#define ENERGY_COST_RESTORE    2

/*---------------------------------------------------------------------------*/
/* ✅ Addition: Checkpoint                                                    */
/*---------------------------------------------------------------------------*/
#define CHECKPOINT_MAGIC  0xABCD1234
#define CHECKPOINT_ADDR   0x00000000

typedef struct {
  uint32_t seq_id;
  float    last_temp;
  float    last_light;
} ckpt_data_t;

/*---------------------------------------------------------------------------*/
/* Variables — same as original + additions                                  */
/*---------------------------------------------------------------------------*/
static struct simple_udp_connection udp_conn;
static uint8_t  energy   = 0;
static uint32_t seq_id   = 0;

static uint8_t  energy_prev      = 0;
static uint32_t sends_in_session = 0;
static uint8_t  cp_saved         = 0;

static float    last_temp        = 0.0f;
static float    last_light       = 0.0f;

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

static void
consume_energy(uint8_t cost)
{
  energy = (energy >= cost) ? energy - cost : 0;
}

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
static int
save_checkpoint(void)
{
  ckpt_data_t data;
  uint32_t    magic = CHECKPOINT_MAGIC;

  data.seq_id     = seq_id;
  data.last_temp  = last_temp;
  data.last_light = last_light;

  xmem_erase(XMEM_ERASE_UNIT_SIZE, CHECKPOINT_ADDR);
  xmem_pwrite(&data,  sizeof(data),  CHECKPOINT_ADDR + sizeof(uint32_t));
  xmem_pwrite(&magic, sizeof(magic), CHECKPOINT_ADDR);

  consume_energy(ENERGY_COST_CHECKPOINT);

  LOG_INFO("### CHECKPOINT_SAVED: seq=%lu "
           "temp=%.1f light=%.1f energy=%u%% ###\n",
           (unsigned long)seq_id,
           last_temp, last_light, energy);
  return 0;
}

/*---------------------------------------------------------------------------*/
static int
restore_checkpoint(void)
{
  uint32_t    magic;
  ckpt_data_t data;

  xmem_pread(&magic, sizeof(magic), CHECKPOINT_ADDR);
  if(magic != CHECKPOINT_MAGIC) {
    LOG_INFO("Checkpoint: no valid data\n");
    return -1;
  }

  xmem_pread(&data, sizeof(data), CHECKPOINT_ADDR + sizeof(uint32_t));

  seq_id     = data.seq_id;
  last_temp  = data.last_temp;
  last_light = data.last_light;

  consume_energy(ENERGY_COST_RESTORE);

  LOG_INFO("### CHECKPOINT_RESTORED: seq=%lu "
           "temp=%.1f light=%.1f ###\n",
           (unsigned long)seq_id,
           last_temp, last_light);
  return 0;
}

/*---------------------------------------------------------------------------*/
static void
ml_check(void)
{
  int8_t slope = (int8_t)energy - (int8_t)energy_prev;

  float danger = predict_failure(
    (float)energy,
    (float)slope,
    (float)sends_in_session
  );

  consume_energy(ENERGY_COST_ML);
  if(energy < ENERGY_THRESHOLD) return;

  LOG_INFO("ML | danger=%.0f | energy=%u | "
           "slope=%d | sends=%lu\n",
           danger, energy, slope,
           (unsigned long)sends_in_session);

  if(danger >= 1.0f && !cp_saved) {
    LOG_INFO("### ML: saving checkpoint ###\n");
    save_checkpoint();
    cp_saved = 1;
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

  energy      = ENERGY_MIN_BOOT +
                (random_rand() % (ENERGY_MAX_BOOT -
                                  ENERGY_MIN_BOOT + 1));
  energy_prev = energy;

  LOG_INFO("Node waking up — energy=%u%%\n", energy);

  xmem_init();
  if(restore_checkpoint() == 0) {
    LOG_INFO("RECOVERY: seq=%lu | energy=%u%%\n",
             (unsigned long)seq_id, energy);
  } else {
    seq_id     = 0;
    last_temp  = 0.0f;
    last_light = 0.0f;
  }
  cp_saved         = 0;
  sends_in_session = 0;

  LOG_INFO("Phase 2: Waking up — waiting %u seconds for RPL\n",
           RPL_SETTLE_TIME / CLOCK_SECOND);

  etimer_set(&t, RPL_SETTLE_TIME);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&t));

  consume_energy(ENERGY_COST_RPL);

  LOG_INFO("Phase 3: RPL settled | energy=%u%%\n", energy);

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

  LOG_INFO("Client started | seq=%lu | energy=%u%%\n",
           (unsigned long)seq_id, energy);

  etimer_set(&send_timer, random_rand() % SEND_INTERVAL);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));

    energy_prev = energy;
    consume_energy(ENERGY_COST_IDLE);
    check_failure();

    if(NETSTACK_ROUTING.node_is_reachable() &&
       NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {



      lps331ap_read_temp(&temp_raw);
      temp  = 42.5f + ((float)temp_raw / 480.0f);
      light = isl29020_read_sample();
      consume_energy(ENERGY_COST_SENSOR);
      check_failure();   /* FIX 2: missing in original */

      last_temp  = temp;
      last_light = light;

      LOG_INFO("BEMS | Temperature: %.1f C | "
               "Light: %.1f lux | seq: %lu\n",
               temp, light, (unsigned long)seq_id);

      snprintf(payload, sizeof(payload),
               "BEMS | Temperature: %.1f C | "
               "Light: %.1f lux | seq: %lu",
               temp, light, (unsigned long)seq_id);

      simple_udp_sendto(&udp_conn, payload,
                        strlen(payload), &dest_ipaddr);
      seq_id++;
      sends_in_session++;

      uint8_t tx_cost = ENERGY_COST_TX_MIN +
                        (random_rand() % (ENERGY_COST_TX_MAX -
                                          ENERGY_COST_TX_MIN + 1));
      consume_energy(tx_cost);
      check_failure();

      ml_check();
      check_failure();

    } else {

      LOG_INFO("Not reachable yet \n");

    }  /* FIX 1: closing else here — energy_harvest is outside if/else */

    /* This always runs whether a packet was sent or not */
    energy_harvest();
    LOG_INFO("ENERGY | level:%u\n", energy);
    check_failure();

    cp_saved = 0;

    etimer_set(&send_timer, SEND_INTERVAL
               - CLOCK_SECOND +
               (random_rand() % (2 * CLOCK_SECOND)));
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
