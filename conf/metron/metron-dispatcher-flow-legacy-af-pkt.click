/**
 *                      Module: Metron agent
 *                    Platform: FastClick
 *                 Network I/O: Regular kernel interfaces (using DPDK AF_PACKET socket)
 *         Traffic dispatching: Flow rule-based
 *  Header field acting as tag: No header field. The flow rule action is used
 *                              as a tag (i.e., action queuen index X, where X is a core index)
 *        Who tags the packets: The NIC, but tags are not written in the packets
 *               Service chain: Sent by the controller on demand
 *
 * In this configuration, explicit DPDK flow rules are used to dispatch input
 * traffic to multiple NIC hardware queues. Metron associates each hardware
 * queue with a CPU core, on which the Metron controller deploys software
 * pipelines. Each software pipeline interacts with the underlying NICs
 * using AF_PACKET sockets via the respective DPDK PMD. More information
 * about AF_PACKET PMD can be found at:
 *    https://doc.dpdk.org/guides/nics/af_packet.html
 *
 * Each pipeline created by the controller runs as a secondary (slave)
 * process and can be represented as an individual Click configuration
 * as follows:
 *    FromDPDKDevice(DEV, QUEUE X) -> .... -> ToDPDKDevice(DEV, QUEUE X)
 * 
 * Each pipeline uses an Rx/Tx queue pair associated with a CPU core,
 * which runs to completion.
 *
 * The flow rules are derived from the stateless classification operations
 * of the input service chain. The Metron controller is in charge of
 * devising these operations.
 */

/**
 * Deploy as follows (replacing eno1/eno2 with your kernel interfaces):
 * sudo ../../bin/click --dpdk -l 0-1 -w 0000:01:00.0 -w 0000:01:00.1 -v \
        --vdev=eth_af_packet0,iface=eno1,blocksz=4096,framesz=2048,framecnt=512,qpairs=1,qdisc_bypass=0 \
        --vdev=eth_af_packet1,iface=eno2,blocksz=4096,framesz=2048,framecnt=512,qpairs=1,qdisc_bypass=0 \
        -- metron-dispatcher-flow-legacy-af-pkt.click
 * Use -w parameter to declare only the PCI addresses of interfaces eno1 and eno2.
 */

/* Configuration arguments */
define(
	$ifaceName0     eth_af_packet0,
	$ifacePCI0      0000:01:00.0,

	$ifaceName1     eth_af_packet1,
	$ifacePCI1      0000:01:00.1,

	$queues         1,

	$dpdkRxMode     flow,          // DPDK's Flow API dispatcher
	$metronRxMode   flow,

	$agentIp        127.0.0.1,
	$agentPort      80,
	$agentVerbosity true,

	$discoverIp     127.0.0.1,
	$discoverPort   8181,
	$discoverUser   onos,
	$discoverPass   rocks,

	$monitoringMode false
);

/* Instantiate the http server to access Metron's handlers */
http :: HTTPServer(PORT 80);

/* Metron agent */
metron :: Metron(
	NIC               fd0,
	NIC               fd1,                 // NICs to use
	RX_MODE           $metronRxMode,       // Rx filter mode
	AGENT_IP          $agentIp,            // Own IP address
	AGENT_PORT        $agentPort,          // Own HTTP port
	DISCOVER_IP       $discoverIp,         // ONOS's REST API IP address
	DISCOVER_PORT     $discoverPort,       // ONOS's REST API port
	DISCOVER_USER     $discoverUser,       // ONOS's REST API username
	DISCOVER_PASSWORD $discoverPass,       // ONOS's REST API password
	SLAVE_DPDK_ARGS   "-w$ifacePCI0",      // Arguments passed to secondary DPDK processes
	SLAVE_DPDK_ARGS   "-w$ifacePCI1",
	SLAVE_DPDK_ARGS   "--log-level=eal,8",
	MONITORING        $monitoringMode,     // Defines the monitoring behaviour of the agent
	VERBOSE           $agentVerbosity      // Defines the verbosity level of the agent
);

/**
 * NICs are inactive at boot time.
 * Slave processes will be dynamically created by the Metron controller.
 */
fd0 :: FromDPDKDevice(PORT $ifaceName0, MODE $dpdkRxMode, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice(PORT $ifaceName0, N_QUEUES $queues);

fd1 :: FromDPDKDevice(PORT $ifaceName1, MODE $dpdkRxMode, N_QUEUES $queues, ACTIVE false)
	-> Idle
	-> ToDPDKDevice(PORT $ifaceName1, N_QUEUES $queues);

DriverManager(
	wait,
	print "",
	print $(fd0.rules_aggr_stats),
	print $(fd1.rules_aggr_stats),
	print "",
	print "Flushing rules...",
	write metron.flush_nics,
	print "    Done!",
	print "",
	print "Minimum rule installation rate: "$(metron.rule_installation_rate_min fd0)" rules/sec",
	print "Average rule installation rate: "$(metron.rule_installation_rate_avg fd0)" rules/sec",
	print "Maximum rule installation rate: "$(metron.rule_installation_rate_max fd0)" rules/sec",
	print "",
	print "Minimum rule     deletion rate: "$(metron.rule_deletion_rate_min fd0)" rules/sec",
	print "Average rule     deletion rate: "$(metron.rule_deletion_rate_avg fd0)" rules/sec",
	print "Maximum rule     deletion rate: "$(metron.rule_deletion_rate_max fd0)" rules/sec",
	stop
);
