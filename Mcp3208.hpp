#pragma once

#define MCP3208_nCS 15
#define MCP3208_DOUT 13 // MOSI
#define MCP3208_DIN 12 // MISO
#define MCP3208_CLK 14

class Mcp3208
{
public:
    Mcp3208()
    {
        //set pin modes
        pinMode(MCP3208_nCS, OUTPUT);
        pinMode(MCP3208_DOUT, OUTPUT);
        pinMode(MCP3208_DIN, INPUT);
        pinMode(MCP3208_CLK, OUTPUT);
        //disable device to start with
        digitalWrite(MCP3208_nCS,HIGH);
        digitalWrite(MCP3208_DOUT,LOW);
        digitalWrite(MCP3208_CLK,LOW);
    }

    int read(int channel)
    {
        int adcvalue = 0;
        byte commandbits = B11000000; //command bits - start, mode, chn (3), dont care (3)

        //allow channel selection
        commandbits|=(channel<<3);

        digitalWrite(MCP3208_nCS, LOW); //Select adc
        // setup bits to be written
        for (int i=7; i>=3; i--){
            digitalWrite(MCP3208_DOUT, commandbits&1<<i);
            //cycle clock
            digitalWrite(MCP3208_CLK, HIGH);
            //delayMicroseconds(1);
            digitalWrite(MCP3208_CLK, LOW);
            //delayMicroseconds(1);
        }

        digitalWrite(MCP3208_CLK, HIGH);    //ignores 2 null bits
        delayMicroseconds(1);
        digitalWrite(MCP3208_CLK, LOW);
        delayMicroseconds(1);
        digitalWrite(MCP3208_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(MCP3208_CLK, LOW);

        //read bits from adc
        for (int i=11; i>=0; i--){
            adcvalue+=digitalRead(MCP3208_DIN)<<i;
            //cycle clock
            digitalWrite(MCP3208_CLK, HIGH);
            //delayMicroseconds(1);
            digitalWrite(MCP3208_CLK, LOW);
            //delayMicroseconds(1);
        }
        digitalWrite(MCP3208_nCS, HIGH); //turn off device
        return adcvalue;
    }

};
