#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

// ================== RTC CONFIG ==================
ThreeWire myWire(19, 18, 5); // DAT, CLK, RST
RtcDS1302<ThreeWire> Rtc(myWire);

// ================== WIFI CONFIG ==================
const char* ssid = "CLASS_ATTENDANCE";
const char* password = "";

IPAddress localIP(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

DNSServer dnsServer;
AsyncWebServer server(80);

// ================== ADMIN ==================
const char* adminUser = "admin";
const char* adminPass = "1234";

// ================== FILES ==================
#define ATT_FILE "/attendance.csv"
#define DEV_FILE "/devices.txt"

// ================== STUDENT DATABASE ==================
struct Student {
  String usn;
  String name;
  String year;
};

Student students[] = {
  {"MCE25EC046M10", "Kishan Kumar V", "1st Year"},
  {"MCE25EC000M00", "Gagan Bhuvan", "1st Year"},
  {"MCE25EC030M19", "Darshan", "1st Year"}
};

int totalStudents = sizeof(students) / sizeof(students[0]);

// ================== HELPERS ==================
int findStudent(String usn) {
  for (int i = 0; i < totalStudents; i++) {
    if (students[i].usn == usn) return i;
  }
  return -1;
}

bool alreadyUsed(String filename, String value) {
  File f = LittleFS.open(filename, "r");
  if (!f) return false;

  while (f.available()) {
    if (f.readStringUntil('\n').indexOf(value) >= 0) {
      f.close();
      return true;
    }
  }
  f.close();
  return false;
}

void saveToFile(String filename, String data) {
  File f = LittleFS.open(filename, "a");
  if (!f) return;
  f.println(data);
  f.close();
}

// ================== COMMON RESULT PAGE ==================
String resultPage(String title, String message, String color) {
  return "<!DOCTYPE html><html><head>"
         "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
         "<style>"
         "body{font-family:Arial;background:#f4f6f9;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
         ".box{background:white;padding:25px;border-radius:12px;box-shadow:0 10px 25px rgba(0,0,0,.2);text-align:center;width:90%;max-width:350px;}"
         "h2{color:" + color + ";}"
         "a{display:block;margin-top:20px;text-decoration:none;color:#4e73df;font-weight:bold;}"
         "</style></head><body>"
         "<div class='box'><h2>" + title + "</h2><p>" + message + "</p>"
         "<a href='/'>Go Back</a></div></body></html>";
}

// ================== STUDENT PAGE ==================
String studentPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Class Attendance</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
body{
font-family:Arial;
background:linear-gradient(135deg,#4e73df,#1cc88a);
height:100vh;
margin:0;
display:flex;
justify-content:center;
align-items:center;
}
.card{
background:white;
padding:25px;
width:90%;
max-width:350px;
border-radius:12px;
box-shadow:0 10px 25px rgba(0,0,0,0.2);
text-align:center;
}
input{
width:100%;
padding:12px;
font-size:16px;
margin-bottom:15px;
border-radius:8px;
border:1px solid #ccc;
text-align:center;
}
input[type=submit]{
background:#4e73df;
color:white;
border:none;
cursor:pointer;
}
</style>
</head>
<body>
<div class="card">
<h2>Class Attendance</h2>
<form action="/submit">
<input type="text" name="usn" placeholder="Enter USN" required>
<input type="submit" value="Mark Attendance">
</form>
</div>
</body>
</html>
)rawliteral";
}

// ================== ADMIN PAGE ==================
String adminPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Admin Panel</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
body{font-family:Arial;background:#f4f6f9;margin:0;padding:20px;}
.card{background:white;padding:20px;border-radius:10px;box-shadow:0 5px 15px rgba(0,0,0,.2);}
a{display:block;margin:15px 0;color:#4e73df;font-weight:bold;text-decoration:none;}
</style>
</head>
<body>
<div class="card">
<h2>Admin Panel – Class Attendance</h2>
<a href="/export">⬇ Download Attendance CSV</a>
<a href="/clear">🧹 Clear Today Attendance</a>
</div>
</body>
</html>
)rawliteral";
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  LittleFS.begin(true);

  Rtc.Begin();
  if (!Rtc.GetIsRunning()) Rtc.SetIsRunning(true);

  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(localIP, localIP, subnet);
  dnsServer.start(53, "*", localIP);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(200, "text/html", studentPage());
  });

  server.on("/submit", HTTP_GET, [](AsyncWebServerRequest *req){
    String usn = req->getParam("usn")->value();
    String ip = req->client()->remoteIP().toString();

    int idx = findStudent(usn);
    if (idx == -1) {
      req->send(200,"text/html",resultPage("Invalid USN","Please enter a valid USN","red"));
      return;
    }
    if (alreadyUsed(ATT_FILE, usn)) {
      req->send(200,"text/html",resultPage("Already Marked","Attendance already recorded","orange"));
      return;
    }
    if (alreadyUsed(DEV_FILE, ip)) {
      req->send(200,"text/html",resultPage("Device Blocked","This device already submitted attendance","red"));
      return;
    }

    RtcDateTime now = Rtc.GetDateTime();
    char dateStr[11];
    snprintf(dateStr,sizeof(dateStr),"%02u-%02u-%04u",now.Day(),now.Month(),now.Year());

    saveToFile(ATT_FILE, usn + "," + students[idx].name + "," + dateStr + "," + ip);
    saveToFile(DEV_FILE, ip);

    req->send(200,"text/html",resultPage("Success","Attendance Recorded Successfully","green"));
  });

  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->authenticate(adminUser, adminPass)) return req->requestAuthentication();
    req->send(200,"text/html",adminPage());
  });

  server.on("/export", HTTP_GET, [](AsyncWebServerRequest *req){
    req->send(LittleFS, ATT_FILE, "text/plain");
  });

  server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *req){
    LittleFS.remove(ATT_FILE);
    LittleFS.remove(DEV_FILE);
    req->send(200,"text/html",resultPage("Cleared","Attendance data cleared","green"));
  });

  server.onNotFound([](AsyncWebServerRequest *req){ req->redirect("/"); });

  server.begin();
}

// ================== LOOP ==================
void loop() {
  dnsServer.processNextRequest();
}

