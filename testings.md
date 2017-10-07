# Testing

### Data Retrival
To retrieve data from the server, use this URL  
http://datasink.iostation.com:8080/v1/thermo?deviceid=<uuid>&token=<token>[&count=<num_results>]

#### Input
uuid: UUID of the device uploading data  
token: a valid token assigned and stored on the server's tokens field  
count: optional, default 5. The maximum number of results returned  

#### Output
JSON string containing all information about the device found

E.g.:  
http://datasink.iostation.com:8080/v1/thermo?deviceid=300027000347343339373536&token=2727586539&count=2

[{"id":"43","deviceid":"300027000347343339373536","temperature":26.56,"humidity":47.19,"latitude":0,"longitude":0,"updateTime":"2017-10-06T18:59:42.000Z","vbatt":87,"rssi":24},{"id":"42","deviceid":"300027000347343339373536","temperature":26.6,"humidity":46.86,"latitude":0,"longitude":0,"updateTime":"2017-10-06T18:57:38.000Z","vbatt":87,"rssi":24}]

### Data Upload
To upload data to the server, compose a JSON string, surround it by a double quote (") and add jsonStr= before.

#### Input
HTTP POST with parameter named `jsonStr` and value being the JSON containing the following keys:  
* deviceid  
     The UUID of the uploading device  
* temperature  
    The temperature reading form the sensor  
* humidity  
    The humidity reading form the sensor  
* latitude  
     The latitude from the GPS output  
* longitude  
     The longitude from the GPS output  
* updatetime  
      The data time of the update, in yyyy-mm-dd hh:mm:ss format  
* vbatt  
     The battery level in percentage (0-100) 
* rssi  
     The received signal strength, could be vary among different chip vendors

#### Output
JSON containing the status of the upload, either OK ({status:OK}) or ERROR ({status:ERROR}).

For example:  

```
curl -v -d 'jsonStr={"deviceid":"1b0032000547343339373536","temperature":29.23,"humidity":56.23,"latitude":31.182089,"longitude":121.363007,"updatetime":"2016-06-25 16:12:07","vbatt":73,"rssi":13}' http://datasink.iostation.com:8080/v1/thermo

*   Trying 163.47.10.79...
* TCP_NODELAY set
* Connected to datasink.iostation.com (163.47.10.79) port 8080 (#0)
> POST /v1/thermo HTTP/1.1
> Host: datasink.iostation.com:8080
> User-Agent: curl/7.54.0
> Accept: */*
> Content-Length: 184
> Content-Type: application/x-www-form-urlencoded
> 
* upload completely sent off: 184 out of 184 bytes
< HTTP/1.1 200 OK
< X-Powered-By: Express
< Content-Type: application/json; charset=utf-8
< Content-Length: 15
< ETag: W/"f-DHdpl5M+tggzs3vq9DgUyA"
< Date: Fri, 06 Oct 2017 15:46:58 GMT
< Connection: keep-alive
< 
* Connection #0 to host datasink.iostation.com left intact
{"status":"OK"}
```

