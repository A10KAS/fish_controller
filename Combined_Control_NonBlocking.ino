
 * 引脚分配：
 * 舵机：软串口 RX=5, TX=6
 * 步进电机：PUL=9, DIR=8, DIR_NEG=10, ENA=7
 * 
 * 特点：非阻塞状态机设计，舵机和步进电机可同时运行

#include <SoftwareSerial.h>

// ==================== 引脚定义 ====================
#define SERVO_RX_PIN 5
#define SERVO_TX_PIN 6

#define PUL_PIN 9      // 脉冲信号引脚（PWM引脚）
#define DIR_PIN 8      // 方向信号正引脚（DIR+）
#define DIR_NEG_PIN 10 // 方向信号负引脚（DIR-）
#define ENA_PIN 7      // 使能信号引脚

// ==================== 软串口初始化 ====================
SoftwareSerial motorSerial(SERVO_RX_PIN, SERVO_TX_PIN);

// ==================== 舵机控制变量（状态机）====================
char servoMode = 's';           // 当前模式：f/l/r/s
char newServoMode = 's';        // 待切换模式
uint16_t servoMoveTime = 300;   // 舵机动作时间（ms）
unsigned long lastServoTime = 0;// 上次发送命令的时间戳
int servoPhase = 0;             // 用于循环模式的相位（0或1）

// ==================== 步进电机控制变量（状态机）====================
bool motorEnabled = false;      // 电机使能状态
bool motorDirection = HIGH;     // 当前方向（HIGH=正转，LOW=反转）
unsigned long motorStepDelay = 1000;   // 步进延迟（微秒）
volatile int motorStepsRemaining = 0;  // 剩余步数（在主循环中修改）
unsigned long lastPulseTime = 0;       // 上次脉冲时间（微秒）

// ==================== 参数配置 ====================
#define MIN_PULSE_WIDTH_US 2    // 最小脉冲宽度（微秒）
#define DIR_SETUP_TIME_US 3     // 方向信号建立时间（微秒）

// ==================== 函数前向声明 ====================
void disableMotor();
void enableMotor();
void setDirection(bool dir);
void runSteps(int steps);
void increaseSpeed();
void decreaseSpeed();
void printStatus();
void sendServoCommand(int angle, uint16_t timeMs, uint8_t id = 0x01);
void handleServo();             // 舵机状态机
void handleStepper();           // 步进状态机
void processCommand(char* cmd);

// ==================== 初始化 ====================
void setup() {
  Serial.begin(115200);
  motorSerial.begin(115200);
  delay(1000);

  // 步进电机引脚初始化
  pinMode(PUL_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(DIR_NEG_PIN, OUTPUT);
  pinMode(ENA_PIN, OUTPUT);

  digitalWrite(PUL_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(DIR_NEG_PIN, HIGH);
  digitalWrite(ENA_PIN, HIGH);  // 默认禁用

  disableMotor();  // 禁用步进电机

  Serial.println(F("\n=== 合并控制程序 (非阻塞版) ==="));
  Serial.println(F("舵机: f/l/r/s, +s/-s"));
  Serial.println(F("步进: E/D/F/R/S, +M/-M, N<num>, GD<num>, GU<num>, ?"));
  Serial.println();
}

// ==================== 主循环 ====================
void loop() {
  // 1. 处理串口命令（非阻塞）
  if (Serial.available() > 0) {
    char cmdBuffer[16];
    int index = 0;
    while (Serial.available() > 0 && index < 15) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') break;
      if (c >= 32 && c <= 126) cmdBuffer[index++] = c;
      delay(1);
    }
    cmdBuffer[index] = '\0';
    // 去除前后空格
    while (index > 0 && cmdBuffer[index-1] == ' ') cmdBuffer[--index] = '\0';
    int start = 0;
    while (cmdBuffer[start] == ' ' && start < index) start++;
    if (index > start) {
      processCommand(cmdBuffer + start);
    }
  }

  // 2. 舵机状态机
  handleServo();

  // 3. 步进电机状态机
  handleStepper();
}

// ==================== 舵机状态机 ====================
void handleServo() {
  // 模式切换
  if (servoMode != newServoMode) {
    servoMode = newServoMode;
    servoPhase = 0;
    lastServoTime = millis();   // 立即发送第一个命令
    // 强制发送一次（由下面的定时判断触发）
  }

  unsigned long now = millis();
  if (now - lastServoTime >= servoMoveTime) {
    // 到了发送下一个命令的时间
    switch (servoMode) {
      case 'f': // 前进：+45° ↔ -45°
        if (servoPhase == 0) {
          sendServoCommand(45, servoMoveTime, 0x01);
        } else {
          sendServoCommand(-45, servoMoveTime, 0x01);
        }
        servoPhase = !servoPhase;
        break;

      case 'l': // 左转：+45° ↔ 0°
        if (servoPhase == 0) {
          sendServoCommand(45, servoMoveTime, 0x01);
        } else {
          sendServoCommand(0, servoMoveTime, 0x01);
        }
        servoPhase = !servoPhase;
        break;

      case 'r': // 右转：-45° ↔ 0°
        if (servoPhase == 0) {
          sendServoCommand(-45, servoMoveTime, 0x01);
        } else {
          sendServoCommand(0, servoMoveTime, 0x01);
        }
        servoPhase = !servoPhase;
        break;

      case 's': // 停止：发送0°（只发一次也可，但重复无妨）
        sendServoCommand(0, servoMoveTime, 0x01);
        break;

      default:
        servoMode = 's';
        break;
    }
    lastServoTime = now;
  }
}

// ==================== 步进电机状态机 ====================
void handleStepper() {
  if (motorEnabled && motorStepsRemaining > 0) {
    unsigned long now = micros();
    if (now - lastPulseTime >= motorStepDelay) {
      // 产生一个脉冲
      digitalWrite(PUL_PIN, HIGH);
      delayMicroseconds(MIN_PULSE_WIDTH_US);
      digitalWrite(PUL_PIN, LOW);

      lastPulseTime = now;
      motorStepsRemaining--;

      // 如果步数完成，打印信息
      if (motorStepsRemaining == 0) {
        Serial.println(F("[步进] 完成"));
      }
    }
  }
}

// ==================== 命令处理 ====================
void processCommand(char* cmd) {
  if (cmd == NULL || cmd[0] == '\0') return;

  int len = 0;
  while (cmd[len] != '\0' && len < 15) len++;
  char firstChar = cmd[0];
  char firstCharUpper = (firstChar >= 'a' && firstChar <= 'z') ? (firstChar - 32) : firstChar;

  // ========== 舵机控制命令（小写字母） ==========
  if (firstChar >= 'a' && firstChar <= 'z') {
    // 舵机速度调节命令
    if ((len == 2 && cmd[0] == '+' && (cmd[1] == 's' || cmd[1] == 'S'))) {
      if (servoMoveTime <= 5900) {
        servoMoveTime += 100;
      }
      Serial.print(F("[舵机] 速度变慢: "));
      Serial.print(servoMoveTime);
      Serial.println(F(" ms"));
      return;
    } else if ((len == 2 && cmd[0] == '-' && (cmd[1] == 's' || cmd[1] == 'S'))) {
      if (servoMoveTime >= 200) {
        servoMoveTime -= 100;
      }
      Serial.print(F("[舵机] 速度变快: "));
      Serial.print(servoMoveTime);
      Serial.println(F(" ms"));
      return;
    }

    // 舵机运动命令
    if (len == 1 && (firstChar == 'f' || firstChar == 'l' || firstChar == 'r' || firstChar == 's')) {
      if (firstChar != servoMode) {
        newServoMode = firstChar;
        Serial.print(F("[舵机] 指令: "));
        Serial.println(firstChar);
      }
      return;
    }
  }

  // ========== 步进电机控制命令（大写字母或特殊命令） ==========
  switch (firstCharUpper) {
    case 'E':
      if (len == 1) {
        enableMotor();
        Serial.println(F("[步进] 已使能"));
      }
      break;

    case 'D':
      if (len == 1) {
        disableMotor();
        Serial.println(F("[步进] 已禁用"));
      }
      break;

    case 'F':
      if (len == 1) {
        setDirection(HIGH);
        Serial.println(F("[步进] 正转"));
      }
      break;

    case 'R':
      if (len == 1) {
        setDirection(LOW);
        Serial.println(F("[步进] 反转"));
      }
      break;

    case 'S':
      if (len == 1) {
        // 停止：将剩余步数清零即可
        motorStepsRemaining = 0;
        Serial.println(F("[步进] 停止"));
      }
      break;

    case '+':
      if (len == 1 || (len == 2 && (cmd[1] == 'M' || cmd[1] == 'm'))) {
        increaseSpeed();
        Serial.print(F("[步进] 加速: "));
        Serial.print(motorStepDelay);
        Serial.println(F(" us"));
      }
      break;

    case '-':
      if (len == 1 || (len == 2 && (cmd[1] == 'M' || cmd[1] == 'm'))) {
        decreaseSpeed();
        Serial.print(F("[步进] 减速: "));
        Serial.print(motorStepDelay);
        Serial.println(F(" us"));
      }
      break;

    case 'N':
      if (len > 1) {
        int steps = 0;
        for (int i = 1; i < len && i < 10; i++) {
          if (cmd[i] >= '0' && cmd[i] <= '9') {
            steps = steps * 10 + (cmd[i] - '0');
          } else {
            steps = 0;
            break;
          }
        }
        if (steps > 0 && steps <= 100000) {
          runSteps(steps);
        } else {
          Serial.println(F("[错误] 步数 1-100000"));
        }
      }
      break;

    case 'G':
      if (len > 2) {
        char dirChar = cmd[1];
        int steps = 0;
        for (int i = 2; i < len && i < 12; i++) {
          if (cmd[i] >= '0' && cmd[i] <= '9') {
            steps = steps * 10 + (cmd[i] - '0');
          } else {
            steps = 0;
            break;
          }
        }
        if (steps > 0 && steps <= 100000) {
          if (dirChar == 'D' || dirChar == 'd') {
            setDirection(HIGH);
            enableMotor();
            runSteps(steps);
          } else if (dirChar == 'U' || dirChar == 'u') {
            setDirection(LOW);
            enableMotor();
            runSteps(steps);
          } else {
            Serial.println(F("[错误] G 命令格式: GD<num> 或 GU<num>"));
          }
        } else {
          Serial.println(F("[错误] 步数 1-100000"));
        }
      } else {
        Serial.println(F("[错误] G 命令格式: GD<num> 或 GU<num>"));
      }
      break;

    case '?':
      printStatus();
      break;

    default:
      Serial.println(F("[错误] 未知命令"));
      break;
  }
}

// ==================== 舵机通信函数 ====================
void sendServoCommand(int angle, uint16_t timeMs, uint8_t id) {
  if (angle > 180) angle = 180;
  if (angle < -180) angle = -180;
  if (timeMs < 10) timeMs = 10;
  if (timeMs > 60000) timeMs = 60000;

  uint8_t buf[12];
  buf[0] = 0x12;        // 帧头
  buf[1] = 0x4C;
  buf[2] = 0x08;        // 舵机控制命令
  buf[3] = 0x07;        // 数据包长度
  buf[4] = id;

  int16_t angle10 = (int16_t)(angle * 10);
  uint16_t angleRaw = (uint16_t)angle10;
  buf[5] = (uint8_t)(angleRaw & 0xFF);
  buf[6] = (uint8_t)((angleRaw >> 8) & 0xFF);

  uint16_t t = timeMs;
  buf[7] = (uint8_t)(t & 0xFF);
  buf[8] = (uint8_t)((t >> 8) & 0xFF);

  buf[9] = 0x00;        // 执行功率
  buf[10] = 0x00;

  uint16_t sum = 0;
  for (int i = 0; i < 11; i++) sum += buf[i];
  buf[11] = (uint8_t)(sum & 0xFF);

  motorSerial.write(buf, 12);
}

// ==================== 步进电机控制函数 ====================
void enableMotor() {
  digitalWrite(ENA_PIN, LOW);
  motorEnabled = true;
  delayMicroseconds(10);
  // 确保方向正确
  setDirection(motorDirection);
}

void disableMotor() {
  digitalWrite(ENA_PIN, HIGH);
  motorEnabled = false;
}

void setDirection(bool dir) {
  digitalWrite(DIR_PIN, dir);
  digitalWrite(DIR_NEG_PIN, !dir);
  motorDirection = dir;
  delayMicroseconds(DIR_SETUP_TIME_US);
}

void runSteps(int steps) {
  if (!motorEnabled) {
    Serial.println(F("[错误] 电机未使能，输入 E 使能"));
    return;
  }
  // 设置方向信号
  digitalWrite(DIR_PIN, motorDirection);
  digitalWrite(DIR_NEG_PIN, !motorDirection);
  delayMicroseconds(DIR_SETUP_TIME_US);

  motorStepsRemaining = steps;   // 设置剩余步数
  lastPulseTime = micros();       // 重置脉冲计时

  Serial.print(F("[步进] 开始 "));
  Serial.print(steps);
  Serial.print(F(" 步 "));
  Serial.println(motorDirection == HIGH ? F("正转") : F("反转"));
}

void increaseSpeed() {
  if (motorStepDelay > 100) {
    motorStepDelay -= 100;
  } else if (motorStepDelay > 10) {
    motorStepDelay -= 10;
  } else if (motorStepDelay > 1) {
    motorStepDelay -= 1;
  }
  if (motorStepDelay < MIN_PULSE_WIDTH_US) {
    motorStepDelay = MIN_PULSE_WIDTH_US;
  }
}

void decreaseSpeed() {
  if (motorStepDelay < 10) {
    motorStepDelay += 1;
  } else if (motorStepDelay < 100) {
    motorStepDelay += 10;
  } else {
    motorStepDelay += 100;
  }
  if (motorStepDelay > 10000) {
    motorStepDelay = 10000;
  }
}

void printStatus() {
  Serial.println(F("\n=== 状态 ==="));
  Serial.print(F("舵机: "));
  Serial.print(servoMode);
  Serial.print(F(" "));
  Serial.print(servoMoveTime);
  Serial.println(F(" ms"));

  Serial.print(F("步进: "));
  Serial.print(motorEnabled ? F("使能") : F("禁用"));
  Serial.print(F(" "));
  Serial.print(motorDirection == HIGH ? F("正转") : F("反转"));
  Serial.print(F(" "));
  Serial.print(motorStepDelay);
  Serial.print(F(" us"));
  unsigned long steps_per_second = 1000000UL / motorStepDelay;
  Serial.print(F(" "));
  Serial.print(steps_per_second);
  Serial.println(F(" 步/s"));
  Serial.println();
}