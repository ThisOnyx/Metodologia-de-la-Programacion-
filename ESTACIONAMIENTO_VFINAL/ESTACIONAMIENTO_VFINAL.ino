// ==================================================
//  LIBRERÍAS
// ==================================================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ==================================================
//  PINES
// ==================================================
// Servos
const int SERVO_ENT = 11;
const int SERVO_SAL = 10;

// LEDs
const int LED_R_ENT = 2;
const int LED_R_SAL = 3;
const int LED_V_ENT = 5;
const int LED_V_SAL = 6;

// Ultrasonidos
const int TRIG_ENT = 12;
const int ECHO_ENT = 13;
const int TRIG_SAL = A0;
const int ECHO_SAL = A1;

// Sensor de humedad
const int SENSOR_HUMEDAD = 4;

// Driver L298N
const int In1 = 8;
const int In2 = 7;
const int ENA = 9;

// (LCD I2C: SDA = A4, SCL = A5, los usa Wire automáticamente)

// ==================================================
//  CONFIGURACIÓN
// ==================================================
const int TOTAL_LUGARES   = 8;       // Capacidad del estacionamiento
const int DISTANCIA_CM    = 10;      // Distancia para detectar auto
const unsigned long T_ABIERTA = 3000; // Tiempo mínimo con pluma abierta (ms)
const unsigned long T_PARPADEO = 300; // Velocidad del parpadeo (ms)
const int AGUA = LOW;                // Cambia a HIGH si tu módulo es al revés

// ==================================================
//  OBJETOS Y VARIABLES
// ==================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Prueba 0x3F si no muestra nada

struct Pluma {
  Servo servo;
  int ledR, ledV, trig, echo;
  bool abierta;
  unsigned long tApertura;
};

Pluma ent;
Pluma sal;

int ocupados = 0;
bool motorActivo = false;
bool estadoParpadeo = false;
unsigned long tParpadeo = 0;

// ==================================================
//  FUNCIONES: SENSORES
// ==================================================
long medirDistancia(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duracion = pulseIn(echo, HIGH, 30000);  // Máx 30 ms
  if (duracion == 0) return 999;               // Sin eco
  return duracion * 0.034 / 2;                 // cm
}

bool hayAuto(Pluma &p) {
  return medirDistancia(p.trig, p.echo) < DISTANCIA_CM;
}

// ==================================================
//  FUNCIONES: PANTALLA
// ==================================================
void actualizarPantalla() {
  lcd.setCursor(0, 0);
  lcd.print("Libres: ");
  lcd.print(TOTAL_LUGARES - ocupados);
  lcd.print("/");
  lcd.print(TOTAL_LUGARES);
  lcd.print("  ");

  lcd.setCursor(0, 1);
  if (ocupados >= TOTAL_LUGARES) {
    lcd.print("   LLENO        ");
  } else {
    lcd.print("   BIENVENIDO   ");
  }
}

// ==================================================
//  FUNCIONES: SERIAL (comunicación con Python)
// ==================================================
void enviarEstado() {
  // Formato: ESTADO,ocupados,libres,motor
  Serial.print("ESTADO,");
  Serial.print(ocupados);
  Serial.print(",");
  Serial.print(TOTAL_LUGARES - ocupados);
  Serial.print(",");
  Serial.println(motorActivo ? 1 : 0);
}

void revisarSerial() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "RESET") {
      ocupados = 0;
      actualizarPantalla();
      enviarEstado();
    } else if (cmd == "ESTADO") {
      enviarEstado();
    }
  }
}

// ==================================================
//  FUNCIONES: PLUMAS Y LEDs
// ==================================================
void abrirPluma(Pluma &p) {
  p.servo.write(90);
  p.abierta = true;
  p.tApertura = millis();
}

void cerrarPluma(Pluma &p) {
  p.servo.write(0);
  p.abierta = false;
}

void actualizarLeds() {
  // Parpadeo sin delay
  if (millis() - tParpadeo >= T_PARPADEO) {
    tParpadeo = millis();
    estadoParpadeo = !estadoParpadeo;
  }

  bool lleno = (ocupados >= TOTAL_LUGARES);

  // ----- Entrada -----
  if (ent.abierta) {
    digitalWrite(ent.ledR, HIGH);           // Abierta = rojo
    digitalWrite(ent.ledV, LOW);
  } else if (lleno) {
    digitalWrite(ent.ledR, estadoParpadeo); // Lleno = rojo parpadea
    digitalWrite(ent.ledV, LOW);
  } else {
    digitalWrite(ent.ledR, LOW);
    digitalWrite(ent.ledV, HIGH);           // Cerrada = verde
  }

  // ----- Salida -----
  if (sal.abierta) {
    digitalWrite(sal.ledR, HIGH);
    digitalWrite(sal.ledV, LOW);
  } else {
    digitalWrite(sal.ledR, LOW);
    digitalWrite(sal.ledV, HIGH);
  }
}

// ==================================================
//  FUNCIONES: LÓGICA DEL ESTACIONAMIENTO
// ==================================================
void atenderEntrada() {
  if (!ent.abierta) {
    if (ocupados < TOTAL_LUGARES && hayAuto(ent)) {
      abrirPluma(ent);
      ocupados++;
      actualizarPantalla();
      Serial.println("ENTRADA");
      enviarEstado();
    }
  } else if (millis() - ent.tApertura >= T_ABIERTA && !hayAuto(ent)) {
    cerrarPluma(ent);
  }
}

void atenderSalida() {
  if (!sal.abierta) {
    if (ocupados > 0 && hayAuto(sal)) {
      abrirPluma(sal);
      ocupados--;
      actualizarPantalla();
      Serial.println("SALIDA");
      enviarEstado();
    }
  } else if (millis() - sal.tApertura >= T_ABIERTA && !hayAuto(sal)) {
    cerrarPluma(sal);
  }
}

// ==================================================
//  FUNCIONES: MOTOR (activa con agua)
// ==================================================
void controlarMotor() {
  bool hayAgua = (digitalRead(SENSOR_HUMEDAD) == AGUA);

  if (hayAgua != motorActivo) {
    motorActivo = hayAgua;

    if (motorActivo) {
      digitalWrite(In1, HIGH);
      digitalWrite(In2, LOW);
      digitalWrite(ENA, HIGH);
    } else {
      digitalWrite(ENA, LOW);
      digitalWrite(In1, LOW);
      digitalWrite(In2, LOW);
    }

    Serial.println(motorActivo ? "AGUA_DETECTADA" : "AGUA_NORMAL");
    enviarEstado();
  }
}

// ==================================================
//  SETUP
// ==================================================
void setup() {
  Serial.begin(9600);

  // Asignar pines a cada pluma
  ent.ledR = LED_R_ENT; ent.ledV = LED_V_ENT;
  ent.trig = TRIG_ENT;  ent.echo = ECHO_ENT;
  sal.ledR = LED_R_SAL; sal.ledV = LED_V_SAL;
  sal.trig = TRIG_SAL;  sal.echo = ECHO_SAL;

  // Salidas
  pinMode(LED_R_ENT, OUTPUT);
  pinMode(LED_R_SAL, OUTPUT);
  pinMode(LED_V_ENT, OUTPUT);
  pinMode(LED_V_SAL, OUTPUT);
  pinMode(TRIG_ENT, OUTPUT);
  pinMode(TRIG_SAL, OUTPUT);
  pinMode(In1, OUTPUT);
  pinMode(In2, OUTPUT);
  pinMode(ENA, OUTPUT);

  // Entradas
  pinMode(ECHO_ENT, INPUT);
  pinMode(ECHO_SAL, INPUT);
  pinMode(SENSOR_HUMEDAD, INPUT);

  // Servos
  ent.servo.attach(SERVO_ENT);
  sal.servo.attach(SERVO_SAL);
  cerrarPluma(ent);
  cerrarPluma(sal);

  // LCD
  lcd.init();
  lcd.backlight();
  actualizarPantalla();

  enviarEstado();
}

// ==================================================
//  LOOP
// ==================================================
void loop() {
  atenderEntrada();
  atenderSalida();
  controlarMotor();
  actualizarLeds();
  revisarSerial();
}