/*
 * $Id: ethapi.h,v 1.13 2018/01/16 11:00:10 alberto Exp $
 */
#ifndef __ETHAPI_INCLUDED__
#define __ETHAPI_INCLUDED__

#include "etherrors.h"

#define ETHAPI_DEBUG

#ifdef __arm__
	/*
	 * Su PC posso avere o non avere la simulazione ethernet. Su embedded
	 * non ce l'ho __MAI__.
	 */
#undef  ETHAPI_DEBUG
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ETHSAFEDELAY      (2) /* numero di secondi di attesa prima di uscire dal chiamante */
#define DEVICENAME_LEN    64  /* eth0, eth1, enc0p45f-567er... */
#define MACADDRESS_LEN    32  /* ff:ff:ff:ff:ff:ff */
#define IPv4ADDR_LEN      32  /* 192.168.143.254 */
#define IPv6ADDR_LEN      64  /* fe80::a6ba:dbff:fe02:38e1 */
#define NETMASK_LEN       32  /* 192.168.143.254 */
#define GATEWAY_LEN       32  /* 192.168.143.254 */
#define DNS_NAMESERVER    128 /* 192.168.143.254 */
#define DNS_DOMAIN        512 /* google.com */
#define IFACENAME_LEN     512 /* ospedale1-camera-7 */
#define NTPSERVERNAME_LEN 512 /* ntp.pool.debian.timeserver1.org */

#define NETWORK_SIMULATION_DELAY_SECS (0)
#define NETWORK_NTP_DELAY_SECS        (2)
#define NETWORK_LINK_TIMER_SECS       (4)
#define NETWORK_INFO_TIMER_SECS       (NETWORK_LINK_TIMER_SECS * 2 + 1)

typedef enum {
    IPNONE   = 0,
    IPDHCP   = 1,
    IPSTATIC = 2,
} t_connection_type;

typedef struct {
    char addressIPv4[IPv4ADDR_LEN];
    char addressIPv6[IPv6ADDR_LEN];
    char netmask[NETMASK_LEN];
    char gateway[GATEWAY_LEN];
    char dnsserver[DNS_NAMESERVER];
    char dnsdomain[DNS_DOMAIN];
    char deviceName[DEVICENAME_LEN];
    char configPath[IFACENAME_LEN];
    char macaddress[MACADDRESS_LEN];
    char ntpserverName[NTPSERVERNAME_LEN];
    t_connection_type connection;
    int linkStatus;
} t_network_conf;

typedef enum {
    T_IPV4ADDR = 0,
    T_IPV6ADDR,
    T_NETMASK,
    T_GATEWAY,
    T_DNSSERVER,
    T_DNSDOMAIN,
    T_IFACENAME,
    T_CONFIG,
    T_MACADDR,
    T_NTPSERVER,
    T_CONNECTION,
} t_network_conf_idx;

extern int ethGetInfo(t_network_conf *conf);
extern int ethGetLinkStatus(t_network_conf *conf);
extern int ethConnect(t_network_conf *conf);
extern int ethNTPConnect(t_network_conf *conf);
extern int ethPingServer(const char *server);
extern int etherror;

#ifdef __cplusplus
}
#endif

#endif
