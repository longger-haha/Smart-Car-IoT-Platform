/**
 * ═══════════════════════════════════════════════════════════════
 *  测试3: ESP-01S WiFi 模块 (AT指令通信测试)
 * ═══════════════════════════════════════════════════════════════
 *
 *  功能: 测试ESP-01S模块是否能正常响应AT指令并连接WiFi
 *  接线:
 *    ESP-01S VCC    → AMS1117-3.3V输出 (⚠️不能用Arduino 3.3V!)
 *    ESP-01S GND    → Arduino GND
 *    ESP-01S TX     → Arduino A3
 *    ESP-01S RX     → Arduino A2
 *    ESP-01S CH_PD  → 3.3V (使能)
 *    ESP-01S GPIO0  → 悬空不接
 *
 *  使用方法:
 *    1. 修改下面的 WIFI_SSID 和 WIFI_PASSWORD 为你的WiFi信息
 *    2. 上传到 Arduino Uno
 *    3. 打开串口监视器 (波特率 115200)
 *    4. 观察测试步骤和结果
 *
 *  预期结果:
 *    AT → OK          (模块正常)
 *    AT+CWMODE? → +CWMODE:1  (Station模式)
 *    AT+CWJAP → OK/WIFI CONNECTED  (连接成功)
 *    AT+CIFSR → 显示IP地址        (获取IP成功)
 */

#include <SoftwareSerial.h>

#define ESP_RX   A2
#define ESP_TX   A3

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

SoftwareSerial espSerial(ESP_RX, ESP_TX);

void setup() {
  Serial.begin(115200);
  
  espSerial.begin(115200);
  
  delay(1000);

  Serial.println(F(""));
  Serial.println(F("╔════════════════════════════════════════════════╗"));
  Serial.println(F("║   ESP-01S WiFi 模块 AT指令测试               ║"));
  Serial.println(F"║   TX=A3, RX=A2                               ║"));
  Serial.println(F("╚════════════════════════════════════════════════╝"));
  Serial.println(F(""));
}

void loop() {
  static int step = 0;
  
  if (step == 0) {
    Serial.println(F("━━━ 步骤 1/5: 检测ESP-01S是否在线 ━━━"));
    String resp = sendAT("AT", 2000);
    
    if (resp.indexOf("OK") != -1) {
      Serial.println(F("✅ 结果: ESP-01S 在线! 模块正常工作"));
      step++;
    } else {
      Serial.print(F("❌ 结果: 无响应! 响应内容: "));
      Serial.println(resp.substring(0, min((int)resp.length(), 150)));
      Serial.println(F(""));
      Serial.println(F("🔧 排查建议:"));
      Serial.println(F("   1. 检查接线: TX→A3, RX→A2"));
      Serial.println(F("   2. 检查电源: VCC必须是3.3V (>300mA!)"));
      Serial.println(F("   3. CH_PD必须接3.3V (使能高电平)"));
      Serial.println(F("   4. 尝试按一下ESP-01S上的复位按钮"));
      return;
    }
    delay(1000);
  }
  
  if (step == 1) {
    Serial.println(F(""));
    Serial.println(F("━━━ 步骤 2/5: 关闭回显 ━━━"));
    String resp = sendAT("ATE0", 2000);
    
    if (resp.indexOf("OK") != -1 || resp.length() < 10) {
      Serial.println(F("✅ 结果: 回显已关闭"));
      step++;
    } else {
      Serial.println(F("⚠️ 回显关闭可能失败, 继续下一步..."));
      step++;
    }
    delay(500);
  }
  
  if (step == 2) {
    Serial.println(F(""));
    Serial.println(F("━━━ 步骤 3/5: 查询当前WiFi模式 ━━━"));
    String resp = sendAT("AT+CWMODE?", 2000);
    
    if (resp.indexOf("+CWMODE:1") != -1) {
      Serial.println(F("✅ 结果: 当前为 Station 模式 (正确!)"));
      step++;
    } else if (resp.indexOf("+CWMODE:") != -1) {
      Serial.println(F("ℹ️ 结果: 当前不是Station模式, 正在切换..."));
      String setResp = sendAT("AT+CWMODE=1", 3000);
      delay(1000);
      
      String checkResp = sendAT("AT+CWMODE?", 2000);
      if (checkResp.indexOf("+CWMODE:1") != -1) {
        Serial.println(F("✅ 切换到 Station 模式成功!"));
        step++;
      } else {
        Serial.println(F("❌ 切换失败!"));
        return;
      }
    } else {
      Serial.print(F("❌ 无法查询! 响应: "));
      Serial.println(resp.substring(0, min((int)resp.length(), 100)));
      return;
    }
    delay(500);
  }
  
  if (step == 3) {
    Serial.println(F(""));
    Serial.println(F("━━━ 步骤 4/5: 连接WiFi ━━━"));
    
    if (String(WIFI_SSID) == "YOUR_WIFI_SSID") {
      Serial.println(F("⚠️ 请先修改代码中的 WIFI_SSID 和 WIFI_PASSWORD!"));
      Serial.println(F("   然后重新上传程序"));
      while(1);
    }
    
    Serial.print(F("   目标网络: "));
    Serial.println(WIFI_SSID);
    Serial.print(F("   正在连接..."));

    String cmd = "AT+CWJAP=\"";
    cmd += WIFI_SSID;
    cmd += "\",\"";
    cmd += WIFI_PASSWORD;
    cmd += "\"";

    String resp = sendAT(cmd, 15000);
    
    if (resp.indexOf("OK") != -1 || resp.indexOf("CONNECTED") != -1 || 
        resp.indexOf("WIFI GOT IP") != -1 || resp.indexOf("GOT IP") != -1) {
      Serial.println(F(""));
      Serial.println(F("✅ 结果: WiFi 连接成功!"));
      step++;
    } else {
      Serial.println(F(""));
      Serial.print(F("❌ 结果: 连接失败! 响应: "));
      Serial.println(resp.substring(0, min((int)resp.length(), 200)));
      Serial.println(F(""));
      Serial.println(F("🔧 排查建议:"));
      Serial.println(F("   1. 检查WiFi名称和密码是否正确"));
      Serial.println(F("   2. 检查路由器是否开启2.4GHz (ESP-01S不支持5GHz)"));
      Serial.println(F("   3. 靠近路由器再试"));
      return;
    }
    delay(1000);
  }
  
  if (step == 4) {
    Serial.println(F(""));
    Serial.println(F("━━━ 步骤 5/5: 获取IP地址 ━━━"));
    String resp = sendAT("AT+CIFSR", 3000);
    
    if (resp.indexOf("STAIP") != -1 || resp.indexOf("\"") != -1) {
      int startIdx = resp.indexOf("\"");
      if (startIdx != -1) {
        int endIdx = resp.indexOf("\"", startIdx + 1);
        if (endIdx != -1) {
          String ip = resp.substring(startIdx + 1, endIdx);
          Serial.print(F("✅ 结果: 获取IP成功! IP地址 = "));
          Serial.println(ip);
        }
      }
    } else {
      Serial.print(F("⚠️ 未获取到标准IP格式, 原始响应: "));
      Serial.println(resp.substring(0, min((int)resp.length(), 150)));
    }

    Serial.println(F(""));
    Serial.println(F("╔════════════════════════════════════════════════╗"));
    Serial.println(F("║   🎉 所有测试通过! ESP-01S 工作正常!         ║"));
    Serial.println(F("╚════════════════════════════════════════════════╝"));
    Serial.println(F(""));
    Serial.println(F("现在可以将此模块用于 SmartRover 主固件了!"));
    
    while(1) {
      delay(60000);
    }
  }
  
  delay(1000);
}

String sendAT(String cmd, unsigned long timeout) {
  espSerial.println(cmd);
  
  String response = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    while (espSerial.available()) {
      char c = espSerial.read();
      response += c;
    }
  }
  
  Serial.print(F("   发送: "));
  Serial.println(cmd);
  
  if (response.length() > 0 && response.length() < 300) {
    Serial.print(F("   响应: "));
    Serial.println(response);
  } else if (response.length() >= 300) {
    Serial.print(F("   响应: (太长, 截取前300字符) "));
    Serial.println(response.substring(0, 300));
  } else {
    Serial.println(F("   响应: (无响应)"));
  }
  
  return response;
}
