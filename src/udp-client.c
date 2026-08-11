#include "contiki.h"
#include "net/routing/routing.h"
#include "lib/random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "sys/log.h"
#include "periph/lps331ap.h"
#include "periph/isl29020.h"
#define LOG_MODULE "App"
#define LOG_LEVEL  LOG_LEVEL_INFO
#define UDP_CLIENT_PORT  8765
#define UDP_SERVER_PORT  5678
#define SEND_INTERVAL    (60 * CLOCK_SECOND)
static struct simple_udp_connection udp_conn;
PROCESS(udp_client_process, "UDP client");
AUTOSTART_PROCESSES(&udp_client_process);
PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer periodic_timer;
  static unsigned count;
  static char str[64];
  uip_ipaddr_t dest_ipaddr;
  int16_t temp_raw;
  float   temp;
  float   light;
  PROCESS_BEGIN();
  lps331ap_set_datarate(LPS331AP_P_1HZ_T_1HZ);
  isl29020_prepare(ISL29020_LIGHT__AMBIENT,
                   ISL29020_RESOLUTION__16bit,
                   ISL29020_RANGE__1000lux);
  isl29020_sample_continuous();
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, NULL);
  etimer_set(&periodic_timer, random_rand() % SEND_INTERVAL);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    if(NETSTACK_ROUTING.node_is_reachable() &&
       NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      lps331ap_read_temp(&temp_raw);
      temp  = 42.5f + ((float)temp_raw / 480.0f);
      light = isl29020_read_sample();
      /* log on the node */
      LOG_INFO("BEMS | Temperature: %.1f C | Light: %.1f lux | seq: %u\n",
               temp, light, count);
      /* payload for the server — same format */
      snprintf(str, sizeof(str),
               "BEMS | Temperature: %.1f C | Light: %.1f lux | seq: %u",
               temp, light, count);
      simple_udp_sendto(&udp_conn, str, strlen(str), &dest_ipaddr);
      count++;
    } else {
      LOG_INFO("Not reachable yet\n");
    }
    etimer_set(&periodic_timer, SEND_INTERVAL
               - CLOCK_SECOND + (random_rand() % (2 * CLOCK_SECOND)));
  }
  PROCESS_END();
}
