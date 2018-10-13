/**
 * Metron agent's master process.
 *
 * NICs are is Flow Director mode, which enables them to classify and
 * dispatch incoming traffic to the system's CPU cores, according to
 * the Metron controller's commands.
 * Slave metron agents will start on different cores upon a command
 * from the controller.
 */

/**
 * Deploy as follows:
 * sudo bin/click --dpdk -c 0x03  -v -- metron-flow-director.click
 */

/* Configuration arguments */
define(
	$iface0       0,
	$iface1       1,

	$queues       1,

	$dpdkRxMode   flow_dir,
	$metronRxMode flow,

	$agentIp      127.0.0.1,
	$agentPort    80,

	$discoverIp   127.0.0.1,
	$discoverPort 8181,
	$discoverUser onos,
	$discoverPass yourSecret
);

/* Instantiate the http server to access Metron's handlers */
http :: HTTPServer(PORT 80);

/* Metron agent */
metron :: Metron(
	NIC               fd0,
	NIC               fd1,           /* NICs to use */
	RX_MODE           $metronRxMode, /* Rx filter mode */
	AGENT_IP          $agentIp,      /* Own IP address */
	AGENT_PORT        $agentPort,    /* Own HTTP port  */
	DISCOVER_IP       $discoverIp,   /* Controller's IP address */
	DISCOVER_PORT     $discoverPort, /* Controller's port */
	DISCOVER_USER     $discoverUser, /* ONOS' REST API username */
	DISCOVER_PASSWORD $discoverPass  /* Password to access ONOS' REST API */
);

/* NICs are in Flow Director mode */
fd0 :: FromDPDKDevice(PORT $iface0, MODE $dpdkRxMode, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice(PORT $iface0, N_QUEUES $queues);

fd1 :: FromDPDKDevice(PORT $iface1, MODE $dpdkRxMode, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice(PORT $iface1, N_QUEUES $queues);

DriverManager(
	wait,
	print "",
	print $(fd0.rules_aggr_stats),
	print "",
	print "Flushing rules...",
	write metron.delete_rules_all,
	print "    Done!",
	print "",
	print "Minimum rule installation rate: "$(metron.rule_installation_rate_min)" rules/sec",
	print "Average rule installation rate: "$(metron.rule_installation_rate_avg)" rules/sec",
	print "Maximum rule installation rate: "$(metron.rule_installation_rate_max)" rules/sec",
	print "",
	print "Minimum rule     deletion rate: "$(metron.rule_deletion_rate_min)" rules/sec",
	print "Average rule     deletion rate: "$(metron.rule_deletion_rate_avg)" rules/sec",
	print "Maximum rule     deletion rate: "$(metron.rule_deletion_rate_max)" rules/sec",
	stop
);
