
/*
ESCLAVO MODBUS RTU
-------------------
    Código para un WK0201 (o un WK0400 al que se le conecta un WK200-I2C)
    actuando como esclavo Modbus RTU para el subproyecto: Monedero electrónico

EQUIPO DE TRABAJO:
------------------
    Carmen Currás <carmencurras@edu.xunta.es>
    Javier Diz <javier.diz@edu.xunta.es>
    Jorge Domínguez <jorgedominguez@edu.xunta.es>
    Fernando García <fernandogarcia@edu.xunta.es>
    Manuel Martínez <elmanu@edu.xunta.es>
    

DESCRIPCIÓN:
-----------
    Sistema de monedero electrónico con tarjetas RFID basado en Arduino y con comunicación Modbus.
    WK0201 o WK0400 con WK200-I2C: esclavo Modbus RTU que obedece a comandos de un maestro Modbus RTU (WK500)
             

    
NOTAS:
------
      -Programa para esclavo monedero con comunicación Modbus RTU Esclavo
      -Para probrabarlo se usa el Modbus Poll como maestro Modbus RTU y se le envía al esclavo (WK0201 ó WK0400 con WK200-I2C)
       al Holding Register 0 el comando 0x31 y al Holding Register 1 el saldo de la tarjeta (valor entre 1 y 255)
      -Para WK0400 se compila usando: ATMEGA 168
      -Para WK0201 se compila usando: ATMEGA 328
      -Dirección Modbus por defecto=5
      -Baud rate =19200 bps

*/





//Librerías
#include <Wire.h>                // Librería para I2C
#include <avr/pgmspace.h>
#include <EEPROM.h>              // Librería para la memoria EEPROM
#include <SimpleModbusSlave.h>   // Librería para Modbus RTU esclavo


//Defines
#define ADDR_RFID   0x41             // Dirección I2C del lector RFID
#define EP_DirMB 0x00                // Dirección Modbus en la EEPROM del Módulo
#define BAUD 19200                   // Baud Rate

boolean rfid = false;


const byte DirDefectoMB = 5;                            //Dirección Modbus por defecto							 
unsigned char DirMB = EEPROM.read (EP_DirMB);           //Leer dirección Modbus de la EEPROM
byte NuevaDirMB = DirMB;

const byte tiempo_error_com=5;       //Tiempo para determinar que no hay comunicación MB si no llega ninguna trama correcta (segundos)


unsigned long tlectura=0;            //marca el instante de tiempo en el que se lee una tarjeta monedero RFID


unsigned long inicioTemp=0;          //guarda el tiempo en el que comienza la temporización del RELE
long temporizacion = 2000;           //temporización del RELE (milisegundos)



enum 
{     
  COMANDO,           // Comando recibido
  ARG1,              // Argumento del comando
  TOTAL_ERRORS,      // Contador de errores MB
  NUM_TARJ_LOW,      // Bytes bajos del número de la tarjeta monedero RFID
  NUM_TARJ_HIGH,     // Bytes altos del númro de la tarjeta monedero RFID
  TOTAL_REGS_SIZE    // Tamaño
};

unsigned int holdingRegs[TOTAL_REGS_SIZE]; 

  
//-----------------------------------------------------------------------------------------------
void setup() 
{ 
  
  Wire.begin();   // Inicia puerto I2C
  rfidReleOff();  // Poner Relé del módulo lector RFID en OFF
   
  if ((DirMB > 247) || (DirMB < 1)){        // Fuera del rango válido de direcciones MB
          DirMB = DirDefectoMB;
          EEPROM.write (EP_DirMB,DirDefectoMB); 
        }
  NuevaDirMB=DirMB;
  
    
  
  modbus_configure(BAUD, DirMB, 0, TOTAL_REGS_SIZE);
  
 
  holdingRegs[NUM_TARJ_LOW] = 0;    // parte baja del número de la tarjeta monedero RFID
  holdingRegs[NUM_TARJ_HIGH] = 0;   // parte alta del número de la tarjeta monedero RFID
  holdingRegs[TOTAL_ERRORS] = 0;
  
} 

 //-----------------------------------------------------------------------------------------------
 
  void loop()
  { 
    unsigned long numeroTarjeta;
    numeroTarjeta=CompruebaRFID();  // averiguo el número de la tarjeta RFID
    holdingRegs[COMANDO] = 0xFFFF;  // pongo en el array asociado al registro 0 de Modbus el valor 0xFFFF
    holdingRegs[TOTAL_ERRORS] = modbus_update(holdingRegs); 
    if(tlectura!=0)
      {
        if ((millis()-tlectura)>(tiempo_error_com*1000))
        {
          holdingRegs[NUM_TARJ_LOW]=0;     //Borro número de tarjeta de los registros MB del lector RFID
          holdingRegs[NUM_TARJ_HIGH]=0;    //si pasado un tiempo_error_com el maestro no los ha borrado
          tlectura=0;
        }
      }  
    if (holdingRegs[COMANDO] != 0xFFFF) //Ha llegado un comando remoto por Modbus 
      {
        ProcesarComando();
      }   
    DesactivarRELE();
}

  //------------------------------------------------------------------------------------------------------------
  // Comprueba si una tarjeta monedero RFID se ha pasado por el lector RFID.
  // Cuando el lector detecta una tarjeta guardo 4 bytes del número de la tarjeta en dos registros MB del lector
  //------------------------------------------------------------------------------------------------------------
  unsigned long CompruebaRFID() {
   int c=0;
   byte codigo [5]={0,0,0,0,0};
   rfid = false;
   int valorDevuelto=0;
   
   Wire.requestFrom(ADDR_RFID, 1);   
   if (Wire.available()){ 
     c = Wire.read();
     rfid = true;
   }  
   
  if(c == 0x6E) // Tarjeta detectada
  {
    
    Wire.beginTransmission(ADDR_RFID);
    Wire.write(0x52);
    Wire.endTransmission();
    Wire.requestFrom(ADDR_RFID, 5);
    for(int i=0;i<5;i++)
    {
      if(Wire.available()) {codigo[i]=Wire.read();}
    }
       
    if(codigo[4]==0x4E) //Segunda parte de la transmisión del código
    {
      Wire.beginTransmission(ADDR_RFID);
      Wire.write(0x52);
      Wire.endTransmission();
      Wire.requestFrom(ADDR_RFID, 1);   
      if(Wire.available()) {codigo[4]=Wire.read();}     
    }   
    unsigned long numero= ((unsigned long)codigo[1])<<24 | ((unsigned long)codigo[2])<<16 | ((unsigned long)codigo[3])<<8 | (unsigned long)codigo[4];
 
    //guardo en dos posiciones del array asociado a los registros Modbus los 4 bytes del número de la tarjeta monedero RFID
    holdingRegs[NUM_TARJ_HIGH]=(unsigned long)codigo[1]<<8 | (unsigned long)codigo[2];
    holdingRegs[NUM_TARJ_LOW]=(unsigned long)codigo[3]<<8 | (unsigned long)codigo[4];
    tlectura=millis();
  
  }
  
  }
  //-----------------------------------------------------------------------------------
  // Para averiguar qué comando Modbus le ha llegado del WK500 (maestro) por Modbus RTU.
  //-----------------------------------------------------------------------------------
void ProcesarComando()
{
 
  switch (holdingRegs[COMANDO])  // Para averiguar cual es el comando que ha recibido del maestro
  {    
     case 0x01: // Aceptar nueva dirección modbus.  (La dirección se le pasa en ARG1)    
       NuevaDirMB = lowByte (holdingRegs[ARG1]) ;     
       if (DirMB != NuevaDirMB) {EEPROM.write (EP_DirMB,NuevaDirMB);}    
       DirMB = NuevaDirMB;     
       modbus_configure(BAUD, DirMB, 0, TOTAL_REGS_SIZE);
     break;
    
    case 0x31:  // ACK de tarjeta monedero. (Recibo saldo actual de la tarjeta leída en ARG1)
    {
      holdingRegs[NUM_TARJ_LOW]=0;     //Borro número de tarjeta de los registros MB del lector RFID
      holdingRegs[NUM_TARJ_HIGH]=0;  
      if (holdingRegs[ARG1]>0)   //saldo actual de la tarjeta leída
      {
      //permitir compra:   
      rfidReleOn();
      inicioTemp = millis(); //para temporizar el relé 
      }
    }  
 
    case 0x2C:  // NACK de tarjeta monedero (no tiene saldo o no está de alta en el sistema)
         holdingRegs[NUM_TARJ_LOW]=0;     //Borro número de tarjeta de los registros MB del lector RFID
         holdingRegs[NUM_TARJ_HIGH]=0;  
    break;
    
  }
}

//-------------------------------------------------------------------------------------------------
// Desactiva el RELE del módulo lector RFID cuando vence una temporización
//-------------------------------------------------------------------------------------------------
void DesactivarRELE()
{
  if (inicioTemp!=0)
    {
    if(millis()-inicioTemp  > temporizacion)  
              {
              rfidReleOff();
              inicioTemp=0;
              }    
    }         
}


//-----------------------------------------------------------------------------------------------
// Activar RELE del módulo lector RFID
//-----------------------------------------------------------------------------------------------

void rfidReleOn(){
  
      Wire.beginTransmission(0x41);
      Wire.write(0x62);
      Wire.write(0x01);
      Wire.write((byte)0x00);
      Wire.write((byte)0x00);
      Wire.write((byte)0x00);
      Wire.write((byte)0x00);    
      Wire.write((byte)0x00);    
      Wire.write(0x63);        
      Wire.endTransmission();
}

//-----------------------------------------------------------------------------------------------
// Desactivar RELE del módulo lector RFID
//-----------------------------------------------------------------------------------------------
void rfidReleOff(){
      
      Wire.beginTransmission(0x41);
      Wire.write(0x62);
      Wire.write((byte)0x00);
      Wire.write((byte)0x00);
      Wire.write((byte)0x00);
      Wire.write((byte)0x00);
      Wire.write((byte)0x00);    
      Wire.write((byte)0x00);    
      Wire.write(0x62);     
      Wire.endTransmission();    
}

//-----------------------------------------------------------------------------------------------
