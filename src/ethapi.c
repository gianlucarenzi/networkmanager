/*
 * $Id: ethapi.c,v 1.22 2017/09/19 09:37:37 alberto Exp $
 *
 * Libreria di accesso al layer network di Linux/GNU Debian
 * Utilizza chiamate di sistema per configurare, attivare e disattivare
 * la rete.
 *
 */
#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "debug.h"
#include "ethapi.h"
#include "etherrors.h"

#define BUFLEN 128


static char buffer[BUFLEN];

int etherror = ETHNOERR;

#ifdef __cplusplus
extern "C" {
#endif

static int ethGetMac(t_network_conf *conf);
static char *ethGetIPAddr(char *device, int useIPv6);
static int ethGetDefaultGateway(t_network_conf *conf);

/*
 * Questa funzione restituisce:
 * il risultato del comando passatogli
 * in caso di errore viene settata la variabile globale etherror;
 * > 0 se il comando restituisce qualcosa e quello che trova e` in
 * commandoutput.
 */
static char *systemcall(const char *command)
{
    char syscall[512];
    FILE *stream;
    int rval;
    int i;
    char lbuff[BUFLEN];

    etherror = ETHNOERR;

    DBG_N("Enter\n");
    /* Azzeriamo il buffer all'inizio... */
    memset(buffer, 0, sizeof(buffer));
    if (command == NULL)
    {
        DBG_E("Empty Command\n");
        etherror = ETHEMPTYCMD;
        return NULL;
    }
    else
    {
        DBG_V("Command: %s\n", command);
    }

    sprintf(syscall, "%s", command);

    stream = popen(syscall, "r"); /* Leggiamo lo stdout della chiamata */
    if (!stream)
    {
        DBG_E("Error on popen()\n");
        perror("Error on popen()");
        etherror = ETHPOPENERR;
        return NULL;
    }

    memset(lbuff, 0, sizeof(lbuff));
    rval = fread(lbuff, 1, sizeof(lbuff), stream);
    DBG_V("fread() returns: %d\n", rval);
    if (rval < 0)
    {
        DBG_E("Error on reading stream\n");
        etherror = ETHFREADERR;
        pclose(stream);
        return NULL;
    }
    else
    if (rval == 0)
    {
        DBG_V("Nothing to read\n");
        etherror = ETHNOERR;
        pclose(stream);
        return NULL;
     }

    DBG_V("Command %s returns LEN: %d:\n\t\n%s", command, rval, lbuff);
    if (debuglevel > DBG_NOISY)
    {
        for (i = 0; i < (rval - 1); i++)
        {
            printR("[%d] %c -- 0x%02x\n", i, lbuff[i], lbuff[i]);
        }
    }
    /* Eliminiamo il carattere di EOL */
    memcpy(buffer, lbuff, (rval - 1));
    DBG_N("Exit\n");
    pclose(stream);
    return buffer;
}

/*
 * Returns the mac address of the device asked
 */
static int ethGetMac(t_network_conf *conf)
{
    char cmdline[1024];
    char *retval;
    DBG_N("Enter\n");
    if (conf == NULL)
        return ETHBADCONFERR;
    if (conf->deviceName == NULL)
        return ETHDEVICEERR;
    sprintf(cmdline, "/sbin/ip link show %s | grep ether | "
            "awk '{print $2}'", conf->deviceName);
    retval = systemcall(cmdline);
    if (retval != NULL)
    {
        int slen = strlen(retval) > sizeof(conf->macaddress)
            ? sizeof(conf->macaddress) : strlen(retval);
        memset(conf->macaddress, 0, sizeof(conf->macaddress));
        strncpy(conf->macaddress, retval, slen);
        return ETHNOERR;
    }
    else
    {
        return etherror;
    }
}

/*
 * Returns the link status of the device (even if it is configured)
 * using the sysfs driver structure...
 *
 * The /sys/class/net/[DEVICE]/carrier is the link status depending
 * on the driver. 1 means LINK UP, 0 means LINK DOWN
 *
 */
int ethGetLinkStatus(t_network_conf *conf)
{
    FILE *linkStat = NULL;
    char fileName[512];
    char linkStr[1];
    int link;

    DBG_N("Enter %p\n", (void *)conf);
    if (conf == NULL)
    {
        return ETHBADCONFERR;
    }

    if (conf->deviceName == NULL)
    {
        conf->linkStatus = ETHSTATEDOWN;
        return ETHDEVICEERR;
    }

    sprintf(fileName, "/sys/class/net/%s/carrier", conf->deviceName);
    linkStat = fopen(fileName, "r");
    if (!linkStat)
    {
        DBG_E("No %s file present!\n", fileName);
        conf->linkStatus = ETHSTATEDOWN;
        return ETHDEVICEERR;
    }

    fscanf(linkStat, "%s", linkStr);
    fclose(linkStat);

    link = strtoul(linkStr, NULL, 10);
    DBG_N("Link from SYSFS is %s == %d\n", fileName, link);
    conf->linkStatus = link == 1 ? ETHSTATEUP : ETHSTATEDOWN;

    DBG_N("Link Status: %d\n", conf->linkStatus);
    DBG_N("Exit\n");
    return ETHNOERR;
}



static char *ethGetIPAddr(char *device, int useIPv6)
{
    char cmdline[1024];
    char *retval;
    DBG_N("Enter\n");
    if (device == NULL)
        return NULL;
    if (useIPv6)
        sprintf(cmdline, "/sbin/ip addr show dev %s | grep inet | "
                "grep inet6 | awk '{print $2}' | cut -f1 -d '/'", device);
    else
        sprintf(cmdline, "/sbin/ip addr show dev %s | grep inet | "
                "grep global | awk '{print $2}' | cut -f1 -d '/'", device);
    retval = systemcall(cmdline);
    DBG_N("Exit with: %s\n", retval);
    return retval;
}

static char *ethGetAddrIPV4(char *device)
{
    int useIPv6 = 0;
    return ethGetIPAddr(device, useIPv6);
}

static char *ethGetAddrIPV6(char *device)
{
    int useIPv6 = 1;
    return ethGetIPAddr(device, useIPv6);
}

static int ethGetAddr(t_network_conf *conf)
{
    char *configuration = NULL;
    if (conf == NULL)
        return ETHBADCONFERR;
    if (conf->deviceName == NULL)
        return ETHDEVICEERR;
    configuration = ethGetAddrIPV4(conf->deviceName);
    if (configuration != NULL)
    {
        strncpy(conf->addressIPv4, configuration,
                sizeof(conf->addressIPv4) > strlen(configuration)
                ? strlen(configuration) : sizeof(conf->addressIPv4));
    }
    else
    {
        sprintf(conf->addressIPv4, "--");
        return ETHBADCONFERR;
    }
    configuration = ethGetAddrIPV6(conf->deviceName);
    if (configuration != NULL)
    {
        strncpy(conf->addressIPv6, configuration,
                sizeof(conf->addressIPv6) > strlen(configuration)
                ? strlen(configuration) : sizeof(conf->addressIPv6));
    }
    else
    {
        sprintf(conf->addressIPv6, "--");
        return ETHBADCONFERR;
    }
    return ETHNOERR;
}

static int ethGetDefaultGateway(t_network_conf *conf)
{
    char cmdline[1024];
    char *retval;
    if (conf == NULL)
        return ETHBADCONFERR;
    if (conf->deviceName == NULL)
        return ETHDEVICEERR;
    sprintf(cmdline, "/sbin/ip route show dev %s | grep default | "
            "awk '{print $3}'", conf->deviceName);
    retval = systemcall(cmdline);
    if (retval != NULL)
    {
        strncpy(conf->gateway, retval,
                sizeof(conf->gateway) > strlen(retval)
                ? strlen(retval) : sizeof(conf->gateway));
        return ETHNOERR;
    }
    else
    {
        /* Il non avere gateway non e` un errore */
        sprintf(conf->gateway, "--");
        return ETHNOERR;
    }
}

static int ethGetNetMask(t_network_conf *conf)
{
    char cmdline[1024];
    char *retval;
    if (conf == NULL)
        return ETHBADCONFERR;
    if (conf->deviceName == NULL)
        return ETHDEVICEERR;
    sprintf(cmdline, "/sbin/ifconfig %s | awk '/Mask:/{ print $4;} ' | "
            "cut -f2 -d ':'", conf->deviceName);
    retval = systemcall(cmdline);
    if (retval != NULL)
    {
        strncpy(conf->netmask, retval,
                sizeof(conf->netmask) >= strlen(retval)
                ? strlen(retval) : sizeof(conf->netmask));
        return ETHNOERR;
    }
    else
    {
        sprintf(conf->netmask, "-");
        return ETHBADCONFERR;
    }
}

static void ethMacConvert(char *dest, char *source)
{
    unsigned int i;
    int k;
    DBG_N("Enter with: %s\n", source);
    k = 0;
    for (i = 0; i < (strlen(source) - 1); i++)
    {
        if (*(source+i) != ':')
        {
            /* Non ho trovato i ':'. Li memorizzo nel nome */
            *(dest+k) = *(source+i);
            k++;
        }
    }
    DBG_N("Exit with: %s\n", dest);
}

static int ethGetNTPServer(t_network_conf *conf)
{
    int rval = ETHNOERR;
    DBG_N("Enter\n");
#ifndef ETHAPI_DEBUG
    FILE *ntpConf;
    /*
     * Partiamo dal presupposto che nel sistema reale
     * l'ultima riga di /etc/ntp.conf e` il server ntp configurato
     * dall'utente
     */
    ntpConf = fopen("/etc/ntp.conf.orig", "r");
    if (ntpConf == NULL)
    {
        DBG_E("No NTP Configured\n");
        sprintf(conf->ntpserverName, "--");
        rval = ETHBADCONFERR;
    }
    else
    {
        /*
         * Se abbiamo il backup, allora possiamo ragionevolmente pensare
         * che abbiamo il server ntp in fondo al file e che quindi la
         * nostra configurazione sia corretta
         */
        char *retval;
        char cmd[512];
        fclose(ntpConf);
        sprintf(cmd, "tail -n 1 /etc/ntp.conf | awk '{print $2}'");
        retval = systemcall(cmd);
        if (retval != NULL)
        {
            strncpy(conf->ntpserverName, retval, strlen(retval));
            rval = ETHNOERR;
        }
        else
        {
            DBG_E("Error on getting ntp.conf data\n");
            sprintf(conf->ntpserverName, "--");
            rval = ETHBADCONFERR;
        }
    }
#else
    conf = conf; // avoid gcc warning
    rval = ETHNOERR;
    DBG_V("Simulazione: Nessun errore\n");
#endif
    DBG_N("Exit with: %d\n", rval);
    return rval;
}


static int ethGetDNSServers(t_network_conf *conf)
{
    int rval = ETHNOERR;
    FILE *resolvConf;
    char * resolv = conf->dnsserver;

    DBG_N("Enter\n");
    /*
     * Partiamo dal presupposto che ogni riga di /etc/resolv.conf
     * che contiene nameserver come parola chiave quella che deve essere sostituita
     * nella struttura nel campo dnsserver (massimo 2 separate da spazio)
     */
    resolvConf = fopen("/etc/resolv.conf", "r");
    if (resolvConf == NULL)
    {
        DBG_E("No DNS Configured\n");
        conf->dnsserver[0] = '\0';
        rval = ETHBADCONFERR;
    }
    else
    {
        /*
         * Se abbiamo il file allora lo scansiono per ogni riga verificando
         * la presenza AL MASSIMO di due righe di nameserver.
         */
        int count = 0;
        char confRaw[4096];
        char * token;
        char * saveptr = confRaw;

        for (;;)
        {
            /* Se posso leggere il file... */
            if (fgets(confRaw, 4096, resolvConf) != NULL)
            {
                if (count < 2)
                {
                    int r = strncmp("nameserver ", confRaw, 10);
                    if ( r == 0)
                    {
                        /* Ho trovato il nameserver: e` seguente
                         * al nome nameserver
                         */
                        token = NULL;
                        token = strtok_r(confRaw, "nameserver ", &saveptr);
                        if (token != NULL)
                        {
                            // Occorre eliminare il carattere di capo-riga
                            *(token + strlen(token) - 1) = ' ';
                            DBG_N("FOUND: NAMESERVER %s\n", token);
                            sprintf(resolv, "%s ", token);
                            resolv += strlen(token) + 1;
                            count++;
                        }
                    }
                }
                else
                {
                    break;
                }
            }
            else
                break;
        }

        if (count == 0)
        {
            DBG_E("Error on getting resolv.conf data\n");
            conf->dnsserver[0] = '\0';
            rval = ETHBADCONFERR;
        }
        else
        {
            DBG_N("FOUND: %s\n", conf->dnsserver);
        }
    }
    if (resolvConf)
        fclose(resolvConf);
    DBG_N("Exit with: %d\n", rval);
    return rval;
}


/*
 * La configurazione e` del tipo:
 * [device]-[MACADDRESS].conf
 * ------------------------------
 * Se il device non fosse specificato, viene impostato di default
 * eth0.
 * Il macaddress e` specifico per ogni scheda.
 *
 */
static void ethConfigFileName(const char *device, const char *configPath, char *filename)
{
    char ethFileName[256];
    char completeFileName[512];
    char macaddress[20];
    char defaultDevice[16];
    t_network_conf conf;
    int rval = 0;

    // Azzeriamo gli array
    memset(&conf, 0, sizeof(t_network_conf));
    memset(ethFileName, 0, sizeof(ethFileName));
    memset(macaddress, 0, sizeof(macaddress));

    DBG_N("Enter\n");
    if (device == NULL)
    {
        DBG_I("No device Given. Default to eth0\n");
        sprintf(defaultDevice, "eth0");
    }
    else
    {
        DBG_I("Device: %s\n", device);
        sprintf(defaultDevice, "%s", device);
    }

    strcpy(conf.deviceName, defaultDevice);

    rval = ethGetMac(&conf);
    if (rval)
    {
        DBG_E("RVAL: %d\n", rval);
    }
    /*
     * Convertiamo il macaddress dal formato:
     * 01:23:45:67:89:ab  in 0123456789ab
     */
    ethMacConvert(macaddress, conf.macaddress);
    sprintf(ethFileName, "%s-%s.conf", defaultDevice, macaddress);
    DBG_V("File Name Will be: %s\n", ethFileName);
    /*
     * Adesso il nome del file sara`:
     * eth0-0123456789ab.conf
     */
    if (configPath != NULL)
        sprintf(completeFileName, "%s%s", configPath, ethFileName);
    else
        snprintf(completeFileName, sizeof(completeFileName), "./netcfg/%s", ethFileName);
    sprintf(filename, "%s", completeFileName);
    DBG_N("Exit\n");
}


/*
 * Questa funzione restituisce 1 se la rete in esame e` gia` stata
 * configurata
 */
static int ethConfig(const char *device, const char *configPath)
{
    FILE *ethconfig = NULL;
    int rval = 0; /* Not configured by default */
    char configName[128];
    DBG_N("Enter\n");
    if (device == NULL)
        return ETHDEVICEERR;
    DBG_V("Checking for existing configuration of %s\n", device);
    ethConfigFileName(device, configPath, configName);
    DBG_V("Filename should be: %s\n", configName);
    ethconfig = fopen(configName, "r");
    if (ethconfig == NULL)
    {
        DBG_E("FileName %s Does not open\n", configName);
        rval = 0;
    }
    else
    {
        DBG_V("Configuration File %s exists Ok\n", configName);
        rval = 1;
        fclose(ethconfig);
    }
    DBG_N("Exit with %d\n", rval);
    return rval;
}

/**
static int ethIsConfigured(t_network_conf *conf)
{
    int rval = ETHNOERR;
    DBG_N("Enter\n");
    if (conf == NULL)
    {
        DBG_E("Bad Configuration\n");
        rval = ETHBADCONFERR;
    }
    else
    {
        if (conf->deviceName == NULL)
        {
            DBG_E("Bad Device Name\n");
            rval = ETHDEVICEERR;
        }
        else
        {
            rval = ethConfig(conf->deviceName, conf->configPath);
        }
    }
    DBG_N("Exit with: %d\n", rval);
    return rval;
}
**/

static int ethCreateConfigFile(t_network_conf *conf)
{
    int rval = ETHNOERR;
    FILE *ethconfFile = NULL;

    DBG_N("Enter\n");
    if (conf == NULL)
    {
        DBG_E("Error! No configuration given\n");
        rval = ETHBADCONFERR;
    }
    else
    {
        if (conf->deviceName == NULL)
        {
            DBG_E("Error! No device given\n");
            rval = ETHDEVICEERR;
        }
        else
        {
            char ethConfFile[512];

            if (!ethConfig(conf->deviceName, conf->configPath))
            {
                /*
                 * Se non esiste, allora ne determino il nome del file
                 * di configurazione
                 */
                ethConfigFileName(conf->deviceName, conf->configPath, ethConfFile);
                DBG_N("ConfFile for device %s: [%s]\n", conf->deviceName,
                      ethConfFile);
                ethconfFile = fopen(ethConfFile, "w");
                if (ethconfFile == NULL)
                {
                    DBG_E("Unable to create ConfFile: [%s]\n",
                          ethConfFile);
                    rval = ETHBADCONFERR;
                }
                else
                {
                    /*
                     * Dobbiamo differenziare la modalita` di scrittura
                     * del file di configurazione, se con dhcp (dinamico)
                     * oppure statico
                     */
                    DBG_N("TYPE=%s\n", conf->connection == IPSTATIC
                          ? "STATIC" : "DHCP");
                    if (conf->connection == IPSTATIC)
                    {
                        /* STATIC NETWORK CONFIGURATION */
                        fprintf(ethconfFile, "# STATIC Configuration\n");
                        fprintf(ethconfFile, "iface %s inet static\n",
                                conf->deviceName);
                        fprintf(ethconfFile, "\taddress %s\n",
                                conf->addressIPv4);
                        fprintf(ethconfFile, "\tnetmask %s\n",
                                conf->netmask);
                        fprintf(ethconfFile, "\tgateway %s\n",
                                conf->gateway);
                        fprintf(ethconfFile, "\tdns-domain %s\n",
                                conf->dnsdomain);
                        fprintf(ethconfFile, "\tdns-nameserver %s\n",
                                conf->dnsserver);
                    }
                    else
                    {
                        DBG_N("# DHCP Configuration\n");
                        /* DHCP CONFIGURATION */
                        fprintf(ethconfFile, "# DHCP Configuration\n");
                        fprintf(ethconfFile, "iface %s inet dhcp\n",
                                conf->deviceName);
                        fprintf(ethconfFile, "\n");
                    }
                    /*
                     * Abbiamo creato correttamente il file, adesso lo
                     * chiudiamo...
                     */
                    rval = fclose(ethconfFile);
                    sync();
                }
            }
        }
    }

    DBG_N("exit with: %d\n", rval);
    return rval;
}

/*
 * Questa funzione si occupa della connessione effettiva, una volta
 * configurata la scheda...
 *
 * ATTENZIONE: E` bloccante e forka le syscall per l'esecuzione
 * di dhclient (spegnimento e riconfigurazione)
 *
 * Impiega diverso tempo per cui andrebbe chiamata all'interno di
 * un thread!!!
 *
 */
int ethConnect(t_network_conf *conf)
{
    char cli[256];
    int rval = 0;
    char ethConfFile[512];
    static int addr = 1;

    DBG_N("Enter\n");

    if (conf == NULL)
    {
        DBG_E("No valid configuration\n");
        rval = ETHBADCONFERR;
    }
    else
    {
        if (conf->deviceName == NULL)
        {
            DBG_E("No InterfaceName given\n");
            rval = ETHDEVICEERR;
        }
        else
        {
            /*
             * Verifichiamo se vogliamo impostare una configurazione
             * oppure connetterci ad una esistente.
             * Se il campo connection contiene:
             * IPNONE, allora intendo collegarmi come nel file di
             * configurazione (se esiste).
             * Se non dovesse esistere allora ne viene creato uno in
             * DHCP come default...
             */
            if (conf->connection == IPNONE)
            {
                /*
                 * Controllo della esistenza del file di configurazione per
                 * la scheda ethernet richiesta
                 */
                if (!ethConfig(conf->deviceName, conf->configPath))
                {
                    DBG_I("Ethernet Not Configured, "
                          "creating configuration\n");
                    rval = ethCreateConfigFile(conf);
                    /*
                     * Adesso che la rete e` configurata ed esiste
                     * il file necessario di configurazione, richiamiamo
                     * la connessione
                     */
                    if (rval != 0)
                    {
                        DBG_E("Error on creating config file.\n");
                        rval = ETHBADCONFERR;
                    }
                    else
                    {
                        rval = ETHNOERR;
                    }
                }
                else
                {
                    /*
                     * Esiste gia` una configurazione. Utilizziamola!
                     */
                    DBG_V("Network device %s already configured."
                          "Using this configuration...\n",
                          conf->deviceName);
                    rval = ETHNOERR;
                }
            }
            else
            {
                /*
                 * Ho impostato la configurazione in maniera esplicita:
                 * IP STATICO o DHCP, per cui voglio configurare la
                 * connessione in questa maniera.
                 */
                if (!ethConfig(conf->deviceName, conf->configPath))
                {
                    /*
                     * Scheda MAI CONFIGURATA:
                     * -- Creiamo una configurazione esplicita!
                     */
                    DBG_I("Ethernet Not Configured, "
                          "creating configuration\n");
                    rval = ethCreateConfigFile(conf);
                    /*
                     * Adesso che la rete e` configurata ed esiste
                     * il file necessario di configurazione, richiamiamo
                     * la connessione
                     */
                    if (rval != 0)
                    {
                        DBG_E("Error on creating config file.\n");
                        rval = ETHBADCONFERR;
                    }
                    else
                    {
                        rval = ETHNOERR;
                    }
                }
                else
                {
                    /*
                     * Ho configurato la scheda almeno una volta. Ma
                     * siccome voglio impostare una configurazione
                     * (potenzialmente) differente, la ricreo.
                     */
                    char cmdline[1024];
                    ethConfigFileName(conf->deviceName, conf->configPath, ethConfFile);
                    DBG_N("Erasing %s Configuration...\n", ethConfFile);
                    snprintf(cmdline, sizeof(cmdline), "rm %s", ethConfFile);
                    rval = system(cmdline);
                    if (rval != 0)
                    {
                        DBG_E("Error: %d while executing %s\n",
                              rval, cmdline);
                        rval = ETHBADCONFERR;
                    }
                    else
                    {
                        DBG_N("Now creating new configuration...\n");
                        rval = ethCreateConfigFile(conf);
                        /*
                         * Adesso che la rete e` configurata ed esiste
                         * il file necessario di configurazione,
                         * richiamiamo la connessione
                         */
                        if (rval != 0)
                        {
                            DBG_E("Error on creating config file.\n");
                            rval = ETHBADCONFERR;
                        }
                        else
                        {
                            rval = ETHNOERR;
                        }
                    }
                }
            }

            if (rval == ETHNOERR)
            {
                /*
                 * In ogni caso, a questo punto ho il file di configurazione
                 * corretto (nuovo o vecchio che sia)
                 */
                ethConfigFileName(conf->deviceName, conf->configPath, ethConfFile);
#ifndef __arm__
    /*
     * Su PC simulo la connessione ethernet completa (disconnessione,
     * riconnessione con tempistiche fittizie).
     */
    #ifdef ETHAPI_DEBUG
                /*
                 * Simuliamo la risposta arriva dopo n secondi
                 */
                {   int count = 0;
                    while (count++ < NETWORK_SIMULATION_DELAY_SECS)
                    {
                        DBG_N("Waiting Configuring...\n");
                        usleep(1000 * 1000L * 1);
                    };
                };
                DBG_N("Exiting...\n");

                if (conf->connection == IPDHCP)
                {
                    sprintf(conf->addressIPv4, "171.64.88.%d", addr++);
                    if (addr > 255)
                        addr = 0;
                    sprintf(conf->addressIPv6, "fe80::a6ba:dbff:fe02:38e1");
                    sprintf(conf->dnsdomain,   "eurek.it");
                    sprintf(conf->dnsserver,   "1.2.3.4 253.1.2.3");
                    sprintf(conf->netmask,	 "255.255.0.0");
                    sprintf(conf->gateway,	 "165.22.88.44");
                }
                else
                {
                    sprintf(conf->addressIPv6, "fe80::a6ba:dbff:fe02:38e1");
                }
                rval = ETHNOERR;
                goto outNet;
    #endif
#endif
                /* E` da fare??? */
                if (conf->connection == IPDHCP)
                {
                    DBG_V("Leasing any dhcp address...\n");
                    sprintf(cli, "/sbin/dhclient -r %s 1>/dev/null "
                            "2>/dev/null", conf->deviceName);
                    /*
                     * Non controlliamo il valore di ritorno del lease del
                     * dhcp in quanto se non fossimo mai stati connessi
                     * sarebbe come avere un errore...
                     */
                    rval = system(cli);
                    /*
                     * ...allo stesso modo anche il dhclient precedente...
                     */
                    DBG_V("Killing dhclient...\n");
                    sprintf(cli, "killall dhclient 1>/dev/null "
                            "2>/dev/null");
                    rval = system(cli);

                    DBG_V("(Re)Starting dhclient...\n");
                    sprintf(cli, "/sbin/dhclient -nw %s "
                            "1>/dev/null 2>/dev/null", conf->deviceName);
                    rval = system(cli);
                }
                /* Disattiviamo l'interfaccia e la riattiviamo */
                sprintf(cli, "/sbin/ifdown %s", conf->deviceName);
                DBG_N("CMDLINE: %s\n", cli);
                rval = system(cli);
                /*
                 * Non verifichiamo il valore di ritorno, poiche` potevamo
                 * non esserci mai connessi, ed il ifdown provoca un errore
                 * o warning...
                 */
                sprintf(cli, "/sbin/ifup %s -i %s "
                        "1>/dev/null 2>/dev/null",
                        conf->deviceName, ethConfFile);
                DBG_N("CMDLINE: %s\n", cli);
                rval = system(cli);

                if (rval == 0)
                    rval = ETHNOERR;
                else
                    rval = ETHDEVICEERR;
            }
        }
    }
outNet:
    DBG_N("exit with %d\n", rval);
    /*
     * La connessione che passa da uno stato ON-OFF-ON puo` impiegare
     * diversi secondi per essere correttamente impostata.
     * Occorre attendere, per cui NON PUO` ESSERE CHIAMATA DAL THREAD
     * PRINCIPALE altrimenti risulta bloccante!
     */
#ifndef ETHAPI_DEBUG
    usleep(ETHSAFEDELAY * 1000L * 1000);
#endif
    return rval;
}

static int ethGetValidNTPServer(const char *server)
{
    char syscall[512];
    int rval = ETHNOERR;
    int ntpdelay = NETWORK_NTP_DELAY_SECS;
    DBG_N("Enter %s\n", server != NULL ? server : "--NO-SERVER--");
    if (server == NULL)
    {
        DBG_E("Server NTP not set\n");
        rval = ETHNTPSERVERERR;
    }
    else
    {
        int s;
        DBG_V("LOOKING for %s\n", server);
        sprintf(syscall, "/bin/ping %s -c %d 1>/dev/null", server, ntpdelay);
        DBG_N("Calling %s\n", syscall);
        s = system(syscall);
        if (s != 0)
        {
            DBG_E("Unable to reach NTP server %s\n", server);
            rval = ETHNTPSERVERERR;
        }
        else
        {
            DBG_V("SERVER %s FOUND\n", server);
            rval = ETHNOERR;
        }
    }
    DBG_N("Exit with %d\n", rval);
    return rval;
}

int ethPingServer(const char *server)
{
    char syscall[512];
    int rval = ETHNOERR;
    if (server == NULL)
    {
        DBG_E("Server %s not set\n", server);
        rval = ETHNTPSERVERERR;
    }
    else
    {
        int s;
        DBG_N("LOOKING for %s\n", server);
        sprintf(syscall, "/bin/ping %s -c 1 1>/dev/null", server);
        s = system(syscall);
        if (s != 0)
        {
            DBG_E("Unable to reach server %s\n", server);
            rval = ETHNTPSERVERERR;
        }
        else
        {
            DBG_N("SERVER %s FOUND\n", server);
            rval = ETHNOERR;
        }
    }
    DBG_N("Exit with %d\n", rval);
    return rval;
}

/*
 * Questa funzione restituisce tutti i campi impostati da una configurazione
 * pre-esistente se esiste, altrimenti -1 se qualche campo non e` impostato
 *
 * ethGetMac(t_network_conf *conf);
 * ethGetAddr(t_network_conf *conf);
 * ethGetNetMask(t_network_conf *conf);
 * ethGetDefaultGateway(t_network_conf *conf);
 * ethGetNTPServer(t_network_conf *conf);
 * ethGetLinkStatus(t_network_conf *conf);
 */
int ethGetInfo(t_network_conf *conf)
{
    int rval = ETHNOERR;
    DBG_N("Enter\n");
    if (conf == NULL)
    {
        DBG_E("No valid configuration\n");
        rval = ETHBADCONFERR;
    }
    else
    {
        // Inizializziamo tutte le strutture necessarie:
        // ipv4 e ipv6. Le altre verranno riempite all'occorrenza
        memset(conf->addressIPv4, 0, IPv4ADDR_LEN);
        memset(conf->addressIPv6, 0, IPv6ADDR_LEN);
        memset(conf->macaddress,  0, MACADDRESS_LEN);
        memset(conf->netmask,     0, NETMASK_LEN);

        rval |= ethGetAddr(conf);
        DBG_N("ethGetAddr returns: %d\n", rval);
        rval |= ethGetNetMask(conf);
        DBG_N("ethGetNetMask returns: %d\n", rval);
        rval |= ethGetDefaultGateway(conf);
        DBG_N("ethGetDefaultGateway returns: %d\n", rval);
        rval |= ethGetMac(conf);
        DBG_N("ethGetMac returns: %d\n", rval);
//        rval |= ethGetValidNTPServer(conf->ntpserverName);
//        DBG_N("ethGetValidNTPServer returns: %d\n", rval);
        rval |= ethGetNTPServer(conf);
        DBG_N("ethGetNTPServer returns: %d\n", rval);
        rval |= ethGetLinkStatus(conf);
        DBG_N("ethGetLinkStatus returns: %d\n", rval);
        rval |= ethGetDNSServers(conf);
        DBG_N("ethGetDNSServers returns: %d\n", rval);
    }
    DBG_N("Exit with: %d\n", rval);
    return rval;
}


int ethNTPConnect(t_network_conf *conf)
{
    int rval = ETHNOERR;

    DBG_N("Enter\n");
    /*
     * Per utilizzare il servizo ntp (gia` attivo di default sulla
     * scheda embedded, verifichiamo che non sia mai stato modificato
     * rispetto all'originale. Altrimenti facciamo una copia dell'originale
     * e modifichiamolo aggiungendo il campo del server ntp che
     * l'applicazione ci ha fornito.
     *
     * LIMITE: si puo` usare un solo server ntp alla volta!
     */
    if (conf == NULL)
    {
        DBG_E("No valid configuration\n");
        rval = ETHBADCONFERR;
    }
    else
    {
        if (conf->ntpserverName == NULL)
        {
            DBG_E("No valid NTP Server Name given\n");
            rval = ETHNTPSERVERERR;
        }
        else
        {
            rval = ethGetValidNTPServer(conf->ntpserverName);
            if (rval == ETHNOERR)
            {
                /*
                 * Verifichiamo se avessimo gia` fatto una copia del
                 * file di configurazione originale
                 */
                char syscall[512];
    #ifndef __arm__
        /*
         * Su PC simuliamo le chiamate della connessione NTP ma in realta` non
         * le facciamo veramente...
         */
        #ifdef ETHAPI_DEBUG
                /*
                 * Simuliamo la risposta arriva dopo n/2 secondi
                 */
                {
                    int count = 0;
                    while (count++ < (NETWORK_SIMULATION_DELAY_SECS / 2))
                    {
                        DBG_N("Waiting Configuring...\n");
                        usleep(1000 * 1000L * 1);
                    }
                }
                DBG_N("Exiting...\n");
                rval = ETHNOERR;
                goto outNTP;
        #endif
    #endif
                FILE *ntpConf;
                ntpConf = fopen("/etc/ntp.conf.orig", "r");
                if (ntpConf == NULL)
                {
                    DBG_I("Backup /etc/ntp.conf into /etc/ntp.conf.orig\n");
                    sprintf(syscall, "cp /etc/ntp.conf /etc/ntp.conf.orig");
                    rval = system(syscall);
                    if (rval != 0)
                    {
                        DBG_E("Error on creating backup of /etc/ntp.conf "
                              "ERR: %d\n", rval);
                        rval = ETHNTPSERVERERR;
                    }
                }
                else
                {
                    fclose(ntpConf);
                }
                DBG_N("Now modifying the original file...\n");
                sprintf(syscall, "cp /etc/ntp.conf.orig /etc/ntp.conf");
                rval = system(syscall);
                if (rval != 0)
                {
                    DBG_E("Error on creating configuration file from backup "
                          "ERR: %d\n", rval);
                    rval = ETHNTPSERVERERR;
                }
                else
                {
                    /*
                     * Ho il file da modificare aggiungendo la riga in fondo
                     * con il server desiderato...
                     */
                    sprintf(syscall, "echo 'server %s' >> /etc/ntp.conf",
                            conf->ntpserverName);
                    rval = system(syscall);
                    if (rval != 0)
                    {
                        DBG_E("Error adding the server %s to /etc/ntp.conf."
                              " Aborting\n", conf->ntpserverName);
                        rval = ETHNTPSERVERERR;
                    }
                    else
                    {
                        /*
                         * Adesso fermiamo e facciamo ripartire il
                         * servizio ntp con il nuovo server!
                         */
                        sprintf(syscall, "service ntp stop 2>/dev/null");
                        rval = system(syscall);
                        /*
                         * Non controllo il valore di ritorno. Potrebbe
                         * essere non essere mai stato attivato...
                         */
                        sprintf(syscall, "service ntp start "
                                "1>/dev/null 2>/dev/null");
                        rval = system(syscall);
                        if (rval != 0)
                        {
                            DBG_E("Something went wrong activating NTP Client"
                                  "\nERR: %d -- CMDLINE: %s\n", rval, syscall);
                            rval = ETHNTPSERVERERR;
                        }
                        else
                        {
                            DBG_N("Service Correctly running at %s\n",
                                  conf->ntpserverName);
                            rval = ETHNOERR;
                        }
                    }
                }
            }
        }
    }
outNTP:
    DBG_N("Exit with %d\n", rval);
    /*
     * La connessione al server NTP che passa da uno stato ON-OFF-ON
     * puo` impiegare diversi secondi per essere correttamente impostata.
     * Occorre attendere, per cui NON PUO` ESSERE CHIAMATA DAL THREAD
     * PRINCIPALE altrimenti risulta bloccante!
     */
#ifndef ETHAPI_DEBUG
    usleep(ETHSAFEDELAY * 1000L * 1000);
#endif
    return rval;
}

#ifdef __cplusplus
}
#endif
