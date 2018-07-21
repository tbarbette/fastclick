/**
 * Metron agent's master process.
 *
 * NICs are is VMDq mode, waiting for tagged packets to be
 * distributed across their queues.
 * Destination MAC address is used as a tag.
 * Slave metron agents will start on different cores upon
 * a command from the controller.
 */

/**
 * Deploy as follows:
 * sudo bin/click --dpdk -c 0xffff  -v -- metron-vmdq.click
 */

/* Configuration arguments */
define(
	$iface0       0,
	$iface1       1,

	$vfPools      16,
	$queues       32,

	$dpdkRxMode   vmdq,
	$metronRxMode mac,

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

/* NICs are in VMDq mode */
fd0 :: FromDPDKDevice(PORT $iface0, MODE $dpdkRxMode, VF_POOLS $vfPools, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice(PORT $iface0, N_QUEUES $queues);

fd1 :: FromDPDKDevice(PORT $iface1, MODE $dpdkRxMode, VF_POOLS $vfPools, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice(PORT $iface1, N_QUEUES $queues);
