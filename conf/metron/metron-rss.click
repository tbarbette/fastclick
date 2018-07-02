/**
 * Metron agent's master process.
 *
 * NICs are is RSS mode, which restricts
 * Metron's dispatching but is used for
 * compatibility (with FastClick) reasons.
 */

/**
 * Deploy as follows:
 * sudo bin/click --dpdk -c 0xff  -v -- metron-rss.click
 */

/* Configuration arguments */
define(
	$iface0       0,
	$iface1       1,

	$queues       32,

	$dpdkRxMode   rss,
	$metronRxMode rss,

	$agentId     "metron:nfv:000001",
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
	ID                $agentId,      /* Agent ID */
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

/* NICs are in RSS mode */
fd0 :: FromDPDKDevice($iface0, MODE $dpdkRxMode, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice($iface0, N_QUEUES $queues);

fd1 :: FromDPDKDevice($iface1, MODE $dpdkRxMode, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice($iface1, N_QUEUES $queues);
