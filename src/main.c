#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/inotify.h>
#include <limits.h>
#include <getopt.h>

#include "debug.h"
#include "ethapi.h" // For ethapi functions
#include <dbus/dbus.h> // For D-Bus communication

// Global D-Bus connection
static DBusConnection *connection = NULL;

// D-Bus constants
const char* DBUS_OBJECT_PATH = "/com/example/NetworkManager";
const char* DBUS_INTERFACE_NAME = "com.example.NetworkManager";

int debuglevel = DBG_INFO;

#define MAX_LINE_LEN 256

// --- Network Configuration Struct ---
typedef struct {
	char ip_addr[MAX_LINE_LEN];
	char netmask[MAX_LINE_LEN];
	char gateway[MAX_LINE_LEN];
	char dns1[MAX_LINE_LEN];
	char dns2[MAX_LINE_LEN];
} StaticNetConfig;

// --- Function Prototypes ---
bool parse_static_config(const char* filename, StaticNetConfig* config);
void apply_static_config(const char* device_name, const StaticNetConfig* config);
void apply_dhcp_config(const char* device_name);
void remove_network_config(const char* device_name);
bool is_link_up(const char* device_name);
void handle_link_change(const char* device_name, bool use_static_config, const StaticNetConfig* static_config);

// --- Main Application ---
int main(int argc, char *argv[]) {
	char* device_name = "eth0";
	char* config_file = "network.conf";
	// --- Argomento Parsing ---
	static struct option long_options[] = {
		{"device", required_argument, 0, 'd'}, // Corresponds to -d
		{"config", required_argument, 0, 'c'}, // Corresponds to -c
		{"debug", required_argument, 0, 'D'},  // New option for debug level
		{0, 0, 0, 0} // Terminator
	};

	int opt;
	int long_index = 0;
	// Use getopt_long instead of getopt
	while ((opt = getopt_long(argc, argv, "d:c:D:", long_options, &long_index)) != -1)
	{
		switch (opt)
		{
			case 'd':
				device_name = optarg;
				break;
			case 'c':
				config_file = optarg;
				break;
			case 'D':
			{
				int level = atoi(optarg);
				// Validate level and set debuglevel
				if (level >= DBG_ERROR && level <= DBG_NOISY)
				{
					debuglevel = level;
				}
				else
				{
					fprintf(stderr, "Warning: Invalid debug level \'%s\'. Using default (%d).\n", optarg, DBG_INFO); // Corrected escape sequence
					debuglevel = DBG_INFO; // Default if invalid
				}
				break;
			}
			case '?': // Handle unknown options
			default:
				// Update usage string for new --debug option
				fprintf(stderr, "Usage: %s [-d device_name] [-c config_file] [--debug <level>]\n", argv[0]);
				return EXIT_FAILURE;
		}
	}

	LOG_INFO("Device: %s, File di Configurazione: %s, Debug Level: %d", device_name, config_file, debuglevel);

	// --- Initialize D-Bus connection ---
	DBusError error;
	dbus_error_init(&error);
	connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (dbus_error_is_set(&error))
	{
		LOG_ERROR("Failed to connect to D-Bus system bus: %s", error.message);
		dbus_error_free(&error);
		return EXIT_FAILURE;
	}
	if (connection == NULL)
	{
		return EXIT_FAILURE;
	}
	// Export an object path for emitting signals
	if (!dbus_connection_register_object_path(connection, DBUS_OBJECT_PATH, NULL, NULL))
	{
		LOG_ERROR("Failed to register D-Bus object path %s.", DBUS_OBJECT_PATH);
		dbus_connection_unref(connection); // Clean up connection on error
		connection = NULL;
		return EXIT_FAILURE;
	}
	// --- End D-Bus initialization ---

	// Verifica dei privilegi di root
	if (geteuid() != 0)
	{
		LOG_ERROR("Questo programma richiede privilegi di root. Eseguire con sudo.");
		// D-Bus cleanup needed here if connection was successful
		if (connection)
		{
			dbus_connection_unref(connection);
			connection = NULL;
		}
		return EXIT_FAILURE;
	}

	StaticNetConfig static_config = {0};
	bool use_static_config = parse_static_config(config_file, &static_config);

	if (use_static_config)
	{
		LOG_INFO("File di configurazione '%s' trovato. Verrà usata la configurazione statica.", config_file);
	}
	else
	{
		LOG_INFO("File di configurazione '%s' non trovato. Verrà usato dhclient.", config_file);
	}

	// --- Setup inotify ---
	int fd = inotify_init();
	if (fd < 0)
	{
		LOG_ERROR("Errore in inotify_init: %s", strerror(errno));
		// D-Bus cleanup needed here if connection was successful
		if (connection)
		{
			dbus_connection_unref(connection);
			connection = NULL;
		}
		return EXIT_FAILURE;
	}

	char watch_path[PATH_MAX];
	snprintf(watch_path, sizeof(watch_path), "/sys/class/net/%s/carrier", device_name);

	int wd = inotify_add_watch(fd, watch_path, IN_MODIFY | IN_CREATE);
	if (wd < 0)
	{
		LOG_ERROR("Errore in inotify_add_watch per '%s': %s", watch_path, strerror(errno));
		close(fd);
		if (errno == ENOENT)
		{
			 LOG_ERROR("Interfaccia '%s' non trovata. Il programma non può continuare.", device_name);
		}
		// D-Bus cleanup needed here if connection was successful
		if (connection)
		{
			dbus_connection_unref(connection);
			connection = NULL;
		}
		return EXIT_FAILURE;
	}

	LOG_INFO("In ascolto per cambiamenti di stato su %s...", watch_path);
	
	// Controllo iniziale dello stato del link all'avvio
	handle_link_change(device_name, use_static_config, &static_config);

	// --- Event Loop ---
	char buffer[sizeof(struct inotify_event) + NAME_MAX + 1];
	while (1)
	{
		ssize_t len = read(fd, buffer, sizeof(buffer));
		if (len < 0)
		{
			LOG_ERROR("Errore nella lettura da inotify: %s", strerror(errno));
			break; // Exit loop on read error
		}

		LOG_INFO("Rilevato cambiamento di stato del link per %s.", device_name);
		handle_link_change(device_name, use_static_config, &static_config);
	}

	// Cleanup
	if (connection)
	{
		dbus_connection_unref(connection);
		connection = NULL;
	}
	inotify_rm_watch(fd, wd);
	close(fd);
	return EXIT_SUCCESS;
}

/**
 * @brief Controlla lo stato del link leggendo il file carrier di sysfs.
 */
bool is_link_up(const char* device_name)
{
	char carrier_path[PATH_MAX];
	snprintf(carrier_path, sizeof(carrier_path), "/sys/class/net/%s/carrier", device_name);
	
	FILE *fp = fopen(carrier_path, "r");
	if (!fp)
	{
		return false;
	}
	
	char status = fgetc(fp);
	fclose(fp);
	
	return (status == '1');
}

/**
 * @brief Gestisce il cambiamento di stato del link, verifica la connettività internet con ritentativi e riconfigurazione.
 */
void handle_link_change(const char* device_name, bool use_static_config, const StaticNetConfig* static_config)
{
	sleep(1); // Breve attesa per stabilizzazione

	// --- Using ethapi for link status ---
	t_network_conf conf;
	memset(&conf, 0, sizeof(t_network_conf));
	strncpy(conf.deviceName, device_name, sizeof(conf.deviceName) - 1);

	int link_status_result = ethGetLinkStatus(&conf); // Call ethGetLinkStatus

	if (link_status_result == ETHNOERR && conf.linkStatus == ETHSTATEUP)
	{
		// Check result and status
		LOG_INFO("Link %s: ATTIVO (via ethGetLinkStatus).", device_name);

		// --- Keep existing logic for applying config for now ---
		// NOTE: This part could ideally be refactored to use ethConnect,
		// but that's a more complex change. For now, we keep the custom apply functions.
		if (use_static_config)
		{
			apply_static_config(device_name, static_config);
		}
		else
		{
			apply_dhcp_config(device_name);
		}
		// --- End keeping existing logic ---

		// --- Verification of Internet Connectivity with Retries ---
		const char* internet_server = "8.8.8.8"; // Public DNS server
		int ping_result;
		int attempts = 0;
		const int MAX_ATTEMPTS = 10;
		const int RETRY_DELAY_SEC = 10;

		LOG_INFO("Verifica connettività Internet verso %s...\n", internet_server);
		do
		{
			ping_result = ethPingServer(internet_server);
			if (ping_result == ETHNOERR)
			{
				LOG_INFO("Connettività Internet verificata: server %s raggiungibile.\n", internet_server);
				break; // Exit loop if successful
			}
			else
			{
				attempts++;
				if (attempts < MAX_ATTEMPTS)
				{
					LOG_ERROR("Tentativo %d/%d fallito: server %s NON raggiungibile (codice errore: %d). Riprovo tra %d secondi...\n", attempts, MAX_ATTEMPTS, internet_server, ping_result, RETRY_DELAY_SEC);
					sleep(RETRY_DELAY_SEC);
				}
			}
		} while (attempts < MAX_ATTEMPTS);

		// --- Check if connectivity was established after retries ---
		if (ping_result != ETHNOERR)
		{
			LOG_ERROR("Server %s irraggiungibile dopo %d tentativi. Riconfigurazione rete in corso...\n", internet_server, MAX_ATTEMPTS);
			
			// Reconfigure network from scratch (reset and try DHCP or static based on use_static_config)
			remove_network_config(device_name); // Reset current config
			if (use_static_config)
			{
				apply_static_config(device_name, static_config); // Try static config again
			}
			else
			{
				apply_dhcp_config(device_name);     // Try to reconfigure with DHCP
			}

			// Perform one final ping check after reconfiguration
			sleep(RETRY_DELAY_SEC); // Give some time for DHCP to apply or static config to take effect
			ping_result = ethPingServer(internet_server);

			if (ping_result != ETHNOERR)
			{
				LOG_ERROR("Riconfigurazione fallita: server %s ancora irraggiungibile. Termino il programma.\n", internet_server);
				exit(EXIT_FAILURE); // Terminate the program if still unreachable
			}
			else
			{
				LOG_INFO("Riconfigurazione riuscita: server %s ora raggiungibile.\n", internet_server);
			}
		}
		// --- End Verification ---

	}
	else
	{
		LOG_INFO("Link %s: NON ATTIVO (via ethGetLinkStatus).\n", device_name);
		remove_network_config(device_name);
	}
}

/**
 * @brief Esegue i comandi per impostare una configurazione statica.
 */
void apply_static_config(const char* device_name, const StaticNetConfig* config)
{
	char command[1024];

	LOG_INFO("Applico configurazione statica a %s...\n", device_name);

	// 1. Assicurati che l'interfaccia sia 'up'
	snprintf(command, sizeof(command), "ip link set %s up", device_name);
	LOG_INFO("CMD: %s\n", command);
	system(command);

	// 2. Assegna indirizzo IP e netmask
	snprintf(command, sizeof(command), "ip addr add %s/%s dev %s", config->ip_addr, config->netmask, device_name);
	LOG_INFO("CMD: %s\n", command);
	system(command);

	// 3. Imposta il gateway di default
	if (strlen(config->gateway) > 0)
	{
		snprintf(command, sizeof(command), "ip route add default via %s", config->gateway);
		LOG_INFO("CMD: %s\n", command);
		system(command);
	}

	// 4. Imposta i DNS
	if (strlen(config->dns1) > 0)
	{
		FILE* resolv_conf = fopen("/etc/resolv.conf", "w");
		if (resolv_conf)
		{
			LOG_INFO("Scrivo /etc/resolv.conf...\n");
			fprintf(resolv_conf, "nameserver %s\n", config->dns1);
			if (strlen(config->dns2) > 0)
			{
				fprintf(resolv_conf, "nameserver %s\n", config->dns2);
			}
			fclose(resolv_conf);
		}
		else
		{
			LOG_ERROR("Impossibile aprire /etc/resolv.conf per scrivere i DNS.\n");
		}
	}
}

/**
 * @brief Lancia dhclient sull'interfaccia.
 */
void apply_dhcp_config(const char* device_name)
{
	char command[128];
	LOG_INFO("Avvio dhclient su %s...\n", device_name);
	snprintf(command, sizeof(command), "dhclient %s", device_name);
	system(command);
	// NOTE: Should potentially wait for DHCP to succeed or add a check.
	// For now, assuming it will be handled by subsequent link status checks.
}

/**
 * @brief Rimuove la configurazione di rete (statica o DHCP).
 */
void remove_network_config(const char* device_name)
{
	char command[128];
	LOG_INFO("Rimuovo configurazione di rete da %s...\n", device_name);

	// Termina eventuali processi dhclient per l'interfaccia
	snprintf(command, sizeof(command), "killall dhclient %s", device_name);
	system(command); // Silenzioso

	// Rimuove gli indirizzi IP dall'interfaccia
	snprintf(command, sizeof(command), "ip addr flush dev %s", device_name);
	LOG_INFO("CMD: %s\n", command);
	system(command);
}


/**
 * @brief Esegue il parsing del file di configurazione.
 */
bool parse_static_config(const char* filename, StaticNetConfig* config)
{
	FILE* fp = fopen(filename, "r");
	if (!fp) return false;

	char line[MAX_LINE_LEN];
	while (fgets(line, sizeof(line), fp))
	{
		line[strcspn(line, "\n")] = 0;

		char* key = strtok(line, "=");
		char* value = strtok(NULL, "");

		if (key && value)
		{
			while (*value == ' ' || *value == '\t')
			{
				value++;
			}
			
			if (strcmp(key, "IP_ADDR") == 0) strncpy(config->ip_addr, value, MAX_LINE_LEN - 1);
			else if (strcmp(key, "NETMASK") == 0) strncpy(config->netmask, value, MAX_LINE_LEN - 1);
			else if (strcmp(key, "GATEWAY") == 0) strncpy(config->gateway, value, MAX_LINE_LEN - 1);
			else if (strcmp(key, "DNS1") == 0) strncpy(config->dns1, value, MAX_LINE_LEN - 1);
			else if (strcmp(key, "DNS2") == 0) strncpy(config->dns2, value, MAX_LINE_LEN - 1);
		}
	}

	fclose(fp);
	return true;
}
