#!/usr/bin/env python3
"""
Jetson Orin Nano 通过串口控制 Arduino Uno 的脚本
Arduino 运行合并控制程序（非阻塞版本），串口号 /dev/ttyACM0
"""

import serial
import threading
import time
import sys
import os

# 串口配置
SERIAL_PORT = '/dev/ttyACM0'
BAUDRATE = 115200
TIMEOUT = 0.1  # 读取超时（秒）

class ArduinoController:
    """Arduino 串口控制器"""
    def __init__(self, port=SERIAL_PORT, baudrate=BAUDRATE, timeout=TIMEOUT):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        self.running = False
        self.read_thread = None

    def connect(self):
        """打开串口并启动读取线程"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
            print(f"已连接到 {self.port}，波特率 {self.baudrate}")
            self.running = True
            self.read_thread = threading.Thread(target=self._read_loop, daemon=True)
            self.read_thread.start()
            return True
        except serial.SerialException as e:
            print(f"连接失败: {e}")
            print("请检查：")
            print("  - 设备是否已连接（ls /dev/ttyACM*）")
            print("  - 当前用户是否有读写权限（sudo usermod -a -G dialout $USER 后重启）")
            return False

    def disconnect(self):
        """停止读取线程并关闭串口"""
        self.running = False
        if self.read_thread and self.read_thread.is_alive():
            self.read_thread.join(timeout=1)
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("串口已关闭")

    def send_command(self, cmd):
        """发送命令到 Arduino（自动添加换行符）"""
        if self.ser and self.ser.is_open:
            # 确保命令以换行符结尾
            if not cmd.endswith('\n'):
                cmd += '\n'
            self.ser.write(cmd.encode('utf-8'))
            self.ser.flush()
        else:
            print("串口未连接")

    def _read_loop(self):
        """后台读取线程，打印 Arduino 返回的信息"""
        while self.running and self.ser and self.ser.is_open:
            try:
                if self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[Arduino] {line}")
                else:
                    time.sleep(0.01)
            except Exception as e:
                print(f"读取错误: {e}")
                break

def print_help():
    """打印帮助信息"""
    help_text = """
可用命令（不区分大小写，但建议按原格式）：
 舵机控制：
   f        前进模式（+45° ↔ -45° 循环）
   l        左转模式（+45° ↔ 0° 循环）
   r        右转模式（-45° ↔ 0° 循环）
   s        停止模式（回到0°）
   +s       舵机速度变慢（增加动作时间）
   -s       舵机速度变快（减少动作时间）

 步进电机控制：
   E        使能电机
   D        禁用电机
   F        设置为正转方向
   R        设置为反转方向
   S        停止当前步进运动（清空剩余步数）
   +M 或 +  加速（减小步间隔）
   -M 或 -  减速（增大步间隔）
   N<num>   执行指定步数（如 N1000），使用当前方向
   GD<num>  正转指定步数（如 GD500）
   GU<num>  反转指定步数（如 GU300）
   ?        查询当前状态

其他命令：
   help     显示本帮助
   exit     退出程序
   cls      清屏（仅Linux终端）
"""
    print(help_text)

def clear_screen():
    """清屏"""
    os.system('clear' if os.name == 'posix' else 'cls')

def main():
    print("Arduino 控制器 - 通过串口控制舵机与步进电机")
    print("=" * 50)
    controller = ArduinoController()
    if not controller.connect():
        sys.exit(1)

    # 打印帮助
    print_help()

    try:
        while True:
            try:
                # 读取用户输入
                cmd = input(">>> ").strip()
                if not cmd:
                    continue

                # 处理内部命令
                if cmd.lower() == 'exit':
                    print("退出程序")
                    break
                elif cmd.lower() == 'help':
                    print_help()
                    continue
                elif cmd.lower() == 'cls':
                    clear_screen()
                    continue

                # 发送到 Arduino
                controller.send_command(cmd)

            except KeyboardInterrupt:
                print("\n收到中断，退出")
                break
            except Exception as e:
                print(f"输入错误: {e}")

    finally:
        controller.disconnect()

if __name__ == "__main__":
    main()