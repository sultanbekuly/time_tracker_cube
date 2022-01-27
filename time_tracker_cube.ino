#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

#include "Wire.h"
#include "I2Cdev.h"
#include "MPU6050.h"
MPU6050 mpu;

///////////////////////////////BASIC SETINGS////////////////////////////
const char *ssid = "Network_Name"; //Wifi Network Name
const char *password = "Network_Key"; //Wifi Network Key
const char *authorization = "Basic Y2WG...lGsA=="; //ex: "Basic ABC=="

//////////////////////////content_length///description/////////////////////pid - project_id
const char *trackers[6][3] = {{"97",           "Email check",              "172635855"},
                              {"93",           "Meeting",                  "172635927"},
                              {"97",           "Programming",              "172635927"},
                              {"93",           "Reading",                  "172718034"},
                              {"109",          "Online training courses",  "172718034"},
                              {"84",           "Relax",                    "\"\""     }};

/*
   |X   |Y   |Z
  0|-1  |-3  |46
  1|50  |-5  |-1
  2|-2  |-50 |-8
  3|-50 |0   |-8
  4|2   |50  |0
  5|7   |3   |-50
  */
///////////////+X,-X,+Y,-Y,+Z,-Z
int sides[] = { 1, 3, 4, 2, 0, 5};
int16_t epsilon = 10;

///////////////////////////////BASIC SETINGS////////////////////////////


///////////////////////////////Do not change////////////////////////////
const char *host = "api.track.toggl.com"; // Domain to Server: google.com NOT https://google.com 
const int httpsPort = 443; //HTTPS PORT (default: 443)

String datarx; //Received data as string
long crontimer;
int httpsClientTimeout = 5000; //in millis

int16_t ax, ay, az;
int16_t gx, gy, gz;

int last_60_measurements[60] = {};
int sent_cube_side = -1;
///////////////////////////////Do not change////////////////////////////


void setup() {
  Wire.begin();
  Serial.begin(9600);

  //wifi init
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to wifi: ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("Connected to wifi: ");
  Serial.println(ssid);
  
  //mpu init
  mpu.initialize();
  Serial.println(mpu.testConnection() ? "MPU6050 OK" : "MPU6050 FAIL");
  delay(1000);

  //filling the array with -1 value
  for(int i=0; i<60; i++){ last_60_measurements[i]=-1;}

}



void loop() {
  //Getting data from the sensor:
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  // (-32768.. 32767) 32768/327 (-100.. 100)
  Serial.print(ax/327); Serial.print('\t');
  Serial.print(ay/327); Serial.print('\t');
  Serial.print(az/327); Serial.print('\t');
  
  //Checking the side of the cube:
  int cube_side = -1;
  if(checkSide( 50, ax/327) ){ cube_side=sides[0]; }
  else if(checkSide(-50, ax/327) ){ cube_side=sides[1]; }
  else if(checkSide( 50, ay/327) ){ cube_side=sides[2]; }
  else if(checkSide(-50, ay/327) ){ cube_side=sides[3]; }
  else if(checkSide( 50, az/327) ){ cube_side=sides[4]; }
  else if(checkSide(-50, az/327) ){ cube_side=sides[5]; }
  Serial.print(cube_side); Serial.print('\t');

  //Get if the cube is on stable position:
  //Need to check if the cube on same position for 3 seconds and then start timer.
  //We take data each 50 milliseconds. 3000/50 = 60. So, we need same 60 measurements.
  for (int i=0;i<59;i++){ //moving each measurement to one position forward
    last_60_measurements[i]=last_60_measurements[i+1];//0=1,1=2..
  }
  last_60_measurements[59] = cube_side; //setting last value with new measurement
  
  //checking if stable last 60 measurements are same
  if(checkIfCubeStable(last_60_measurements)){
    Serial.print("Stable position"); Serial.print('\t');
    //checking if sent side is not equal to current cube position:
    if(sent_cube_side!=cube_side){
      Serial.print("SEND DATA!"); Serial.print('\t');
      sent_cube_side = cube_side; 
      Serial.print("callhttps_start_time_entry"); Serial.print('\t');
      //Starting tracking time. POST request.
      callhttps_start_time_entry(trackers[sent_cube_side][0], trackers[sent_cube_side][1], trackers[sent_cube_side][2]);
    } 
  }
  Serial.println("");
  delay(50);
  
}

//Checking if value is approximately equal to searching value
bool checkSide(int16_t side, int16_t a){ //return true or false
  if(side-epsilon < a && a < side+epsilon ){//40 < 50 < 60
      return true;
  }
  return false;
};


//Checking if cube on stable position
bool checkIfCubeStable(int measurements[]){
  for (int i=0;i<60;i++){
    if(i==59){return true;};
    if(measurements[i] != measurements[i+1]){return false;};
  } 
}


//Call https POST request. Start tracking time:
void callhttps_start_time_entry(const char* content_length, const char* description, const char* pid){
  //Setting connection:
  WiFiClientSecure httpsClient;
  httpsClient.setInsecure();   
  httpsClient.setTimeout(httpsClientTimeout);
  delay(1000);
  int retry = 0;
  while ((!httpsClient.connect(host, httpsPort)) && (retry < 15)) {
    delay(100);
    Serial.print(".");
    retry++;
  }
  if (retry == 15) {Serial.println("Connection failed");}
  else {Serial.println("Connected to Server");}

  //Sending the POST request:
  Serial.println("Request_start{");
  String req = String("POST /api/v8/time_entries/start HTTP/1.1\r\n")
        + "Host: api.track.toggl.com\r\n"
        +"Content-Type: application/json\r\n"
        +"Authorization: " + authorization + "\r\n"
        +"Content-Length: " + content_length + "\r\n\r\n"
        
        +"{\"time_entry\":{\"description\":\"" + description + "\",\"tags\":[],\"pid\":" + pid + ",\"created_with\":\"time_cube\"}}" + "\r\n\r\n";
  
  Serial.println(req);
  httpsClient.print(req);
  Serial.println("}Request_end");

  //Response:
  Serial.println("line{");
  while (httpsClient.connected()) {
    String line = httpsClient.readStringUntil('\n');
    Serial.print(line);
    if (line == "\r") {
      break;
    }
  }
  Serial.println("}line");
  
  Serial.println("datarx_start{");
  while (httpsClient.available()) {
    datarx += httpsClient.readStringUntil('\n');
  }
  Serial.println(datarx);
  Serial.println("}datarx_end");
  datarx = "";
}
