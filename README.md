## mod_result_status_counters
__________________________

[![Build Status](https://travis-ci.org/TheMeier/mod_result_status_counter.svg?branch=master)](https://travis-ci.org/TheMeier/mod_result_status_counter)

This apache module counts the result status of each request handled by apache httpd. The counters can be fetched via a module_hablder wicht outputs a simple json array. This is hand to do some monitoruing.

To use compile with apxs, add a location handler an LoadModule directive to your config:

```xml
LoadModule result_status_counter_module modules/mod_result_status_counter.so
<Location /mrsc>
   SetHandler request_status_counter
</Location>
```


FIXME:
I could not find a way to translate the apache result-array indices back to status codes and sadly status_lines is not exortet in http_protocol.h so for now in copy&pasted the definition of status_lines[]


