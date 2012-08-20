watchsnmpd: watch a running snmpd daemon using AgentX
=====================================================

`watchsnmpd` is a tool to watch if `snmpd` is working correctly. It
uses the AgentX protocol. This is mainly a debug tool to find out why
`snmpd` is blocked. It is not a supervision tool. Notably, if `snmpd`
is not running, `watchsnmpd` won't start it.
