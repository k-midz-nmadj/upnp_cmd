================== "upnp_cmd" Parameter Definition for each Action ==================

GetExternalIPAddress : upnp_cmd type(WANIPConnection:1, WANPPPConnection:1, etc...)

GetPortMappingEntry  : upnp_cmd type index

AddPortMapping       : upnp_cmd type protocol port IPaddress [description] [duration]

DeletePortMapping    : upnp_cmd type protocol port
