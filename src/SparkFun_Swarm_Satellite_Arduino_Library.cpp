/*!
 * @file SparkFun_Swarm_Satellite_Arduino_Library.cpp
 * 
 * @mainpage SparkFun Swarm Satellite Arduino Library
 * 
 * @section intro_sec Introduction
 * 
 * This library facilitates communication with the Swarm M138 satellite modem.
 * 
 * Want to support open source hardware? Buy a board from SparkFun!
 * <br>SparkX Swarm Serial Breakout (SPX-19236): https://www.sparkfun.com/products/19236
 *  * 
 * @section author Author
 * 
 * This library was written by:
 * Paul Clark
 * SparkFun Electronics
 * January 2022
 * 
 * @section license License
 * 
 * MIT: please see LICENSE.md for the full license information
 * 
 */

#include "SparkFun_Swarm_Satellite_Arduino_Library.h"

SWARM_M138::SWARM_M138(int gpio1Pin)
{
#ifdef SWARM_M138_SOFTWARE_SERIAL_ENABLED
  _softSerial = NULL;
#endif
  _hardSerial = NULL;
  _baud = SWARM_M138_SERIAL_BAUD_RATE;
  _i2cPort = NULL;
  _address = SFE_QWIIC_SWARM_DEFAULT_I2C_ADDRESS;
  _debugPort = NULL;
  _printDebug = false;
  _gpio1Pin = gpio1Pin;
  _checkUnsolicitedMsgReentrant = false;
  _swarmDateTimeCallback = NULL;
  _lastI2cCheck = millis();

  _swarmDateTimeCallback = NULL;
  _swarmGpsJammingCallback = NULL;
  _swarmGeospatialCallback = NULL;
  _swarmGpsFixQualityCallback = NULL;
  _swarmPowerStatusCallback = NULL;
  _swarmReceiveMessageCallback = NULL;
  _swarmReceiveTestCallback = NULL;
  _swarmSleepWakeCallback = NULL;
  _swarmModemStatusCallback = NULL;
  _swarmTransmitDataCallback = NULL;

}

#ifdef SWARM_M138_SOFTWARE_SERIAL_ENABLED
/**************************************************************************/
/*!
    @brief  Begin communication with the Swarm M138 modem
    @param  softSerial
            The software serial port to be used to communicate with the modem
    @return True if communication with the modem was successful, otherwise false
*/
/**************************************************************************/
bool SWARM_M138::begin(SoftwareSerial &softSerial)
{
  if (!initializeBuffers())
    return false;
    
  _softSerial = &softSerial;

  // There's no 'easy' way to tell if the serial port has already been begun for us.
  // We have to assume it has not been begun and so do it here.
  // For special cases like Software Serial on ESP32, we need to begin _and_ end the port externally
  // _before_ calling the SWARM_M138 .begin.
  beginSerial(_baud);

  return (isConnected());
}
#endif

/**************************************************************************/
/*!
    @brief  Begin communication with the Swarm M138 modem
    @param  hardSerial
            The hardware serial port to be used to communicate with the modem
    @return True if communication with the modem was successful, otherwise false
*/
/**************************************************************************/
bool SWARM_M138::begin(HardwareSerial &hardSerial)
{
  if (!initializeBuffers())
    return false;
    
  _hardSerial = &hardSerial;

  // There's no 'easy' way to tell if the serial port has already been begun for us.
  // We have to assume it has not been begun and so do it here.
  // For special cases like Software Serial on ESP32, we need to begin _and_ end the port externally
  // _before_ calling the SWARM_M138 .begin.
  beginSerial(_baud);

  return (isConnected());
}

/**************************************************************************/
/*!
    @brief  Begin communication with the Swarm M138 modem
    @param  deviceAddress
            The I2C address of the Qwiic Swarm.
            Default is SFE_QWIIC_SWARM_DEFAULT_I2C_ADDRESS 0x52.
    @param  wirePort
            The TwoWire (I2C) port used to communicate with the Power Board.
            Default is Wire.
    @return True if communication with the modem was successful, otherwise false
*/
/**************************************************************************/
bool SWARM_M138::begin(byte deviceAddress, TwoWire &wirePort)
{
  if (!initializeBuffers())
    return false;

  _i2cPort = &wirePort;
  _address = deviceAddress;

  return (isConnected());
}

/**************************************************************************/
/*!
    @brief  Check if the modem is connected and responding by getting
            the configuration settings
    @return True if successful
            False if unsuccessful
*/
/**************************************************************************/
bool SWARM_M138::isConnected(void)
{
  uint32_t dev_ID = 0;
  return (getDeviceID(&dev_ID) == SWARM_M138_ERROR_SUCCESS);
}

// Private: allocate memory for the serial buffers and clear it
bool SWARM_M138::initializeBuffers()
{
  _swarmRxBuffer = new char[_RxBuffSize];
  if (_swarmRxBuffer == NULL)
  {
    if (_printDebug == true)
      _debugPort->println(F("begin: not enough memory for _swarmRxBuffer!"));
    return false;
  }
  memset(_swarmRxBuffer, 0, _RxBuffSize);

  _pruneBuffer = new char[_RxBuffSize];
  if (_pruneBuffer == NULL)
  {
    if (_printDebug == true)
      _debugPort->println(F("begin: not enough memory for _pruneBuffer!"));
    return false;
  }
  memset(_pruneBuffer, 0, _RxBuffSize);

  _swarmBacklog = new char[_RxBuffSize];
  if (_swarmBacklog == NULL)
  {
    if (_printDebug == true)
      _debugPort->println(F("begin: not enough memory for _swarmBacklog!"));
    return false;
  }
  memset(_swarmBacklog, 0, _RxBuffSize);

  commandError = new char[SWARM_M138_MAX_CMD_ERROR_LEN];
  if (commandError == NULL)
  {
    if (_printDebug == true)
      _debugPort->println(F("begin: not enough memory for commandError!"));
    return false;
  }
  memset(commandError, 0, SWARM_M138_MAX_CMD_ERROR_LEN);

  return true;
}

/**************************************************************************/
/*!
    @brief  Enable debug messages on the chosen serial port
            Calling this function with nothing sets the debug port to Serial
            You can also call it with other streams like Serial1, SerialUSB, etc.
    @param  debugPort
            The serial port to be used for the debug messages
*/
/**************************************************************************/
void SWARM_M138::enableDebugging(Stream &debugPort)
{
  _debugPort = &debugPort;
  _printDebug = true;
}

/**************************************************************************/
/*!
    @brief  Disable debug messages
*/
/**************************************************************************/
void SWARM_M138::disableDebugging(void)
{
  _printDebug = false;
}

/**************************************************************************/
/*!
    @brief  Check for the arrival of new serial data. Parse it.
            Process any unsolicited messages. Call the callback(s) - if enabled.
    @return True if at least one unsolicited message was processed, otherwise false
*/
/**************************************************************************/
bool SWARM_M138::checkUnsolicitedMsg(void)
{
  if (_checkUnsolicitedMsgReentrant == true) // Check for reentry (i.e. checkUnsolicitedMsg has been called from inside a callback)
    return false;

  _checkUnsolicitedMsgReentrant = true;

  size_t avail = 0; // The number of available serial bytes
  bool handled = false; // Flag if any unsolicited messages were handled
  unsigned long timeIn = millis(); // Record the time so we can timeout
  char *event; // Each unsolicited messages is an 'event'

  memset(_swarmRxBuffer, 0, _RxBuffSize); // Clear _swarmRxBuffer

  // Does the backlog contain any data? If it does, copy it into _swarmRxBuffer and then clear the backlog
  // All of the serial data from the modem is 'printable'. It should never contain a \0. So it is OK to use strlen.
  size_t backlogLength = strlen((const char *)_swarmBacklog);
  if (backlogLength > 0)
  {
    //The backlog also logs reads from other tasks like transmitting.
    if (_printDebug == true)
    {
      _debugPort->print(F("checkUnsolicitedMsg: backlog found! backlog length is "));
      _debugPort->println(backlogLength);
    }
    memcpy(_swarmRxBuffer + avail, _swarmBacklog, backlogLength); // avail is zero
    avail += backlogLength;
    memset(_swarmBacklog, 0, _RxBuffSize); // Clear the backlog making sure it is NULL-terminated
  }

  int hwAvail = hwAvailable();
  if ((hwAvail > 0) || (backlogLength > 0)) // If either new data is available, or backlog had data.
  {
    // Wait for up to _rxWindowMillis for new serial data to arrive. 
    while (((millis() - timeIn) < _rxWindowMillis) && ((avail + hwAvail) < _RxBuffSize))
    {
      if (hwAvail > 0) //hwAvailable can return -1 if the serial port is NULL
      {
        avail += hwReadChars((char *)&_swarmRxBuffer[avail], hwAvail);
        timeIn = millis();
      }
      hwAvail = hwAvailable();
    }

    // _swarmRxBuffer now contains the backlog (if any) and the new serial data (if any)

    // A health warning about strtok:
    // strtok will convert any delimiters it finds ("\n" in our case) into NULL characters.
    // Also, be very careful that you do not use strtok within an strtok while loop.
    // The next call of strtok(NULL, ...) in the outer loop will use the pointer saved from the inner loop!
    // In our case, strtok is also used in pruneBacklog, which is called by waitForRespone or sendCommandWithResponse,
    // which is called by the parse functions called by processURCEvent...
    // The solution is to use strtok_r - the reentrant version of strtok

    char *preservedEvent;
    event = strtok_r(_swarmRxBuffer, "\n", &preservedEvent); // Look for an 'event' (_swarmRxBuffer contains something ending in \n)

    if (event != NULL)
      if (_printDebug == true)
        _debugPort->println(F("checkUnsolicitedMsg: event(s) found! ===>"));

    while (event != NULL) // Keep going until all events have been processed
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("checkUnsolicitedMsg: start of event: "));
        _debugPort->println(event);
      }

      //Process the event
      bool latestHandled = processUnsolicitedEvent((const char *)event);
      if (latestHandled)
        handled = true; // handled will be true if latestHandled has ever been true

      backlogLength = strlen((const char *)_swarmBacklog);
      if ((backlogLength > 0) && ((avail + backlogLength) < _RxBuffSize)) // Has any new data been added to the backlog?
      {
        if (_printDebug == true)
        {
          _debugPort->println(F("checkUnsolicitedMsg: new backlog added!"));
        }
        memcpy(_swarmRxBuffer + avail, _swarmBacklog, backlogLength);
        avail += backlogLength;
        memset(_swarmBacklog, 0, _RxBuffSize); //Clear out the backlog buffer again.
      }

      //Walk through any remaining events
      event = strtok_r(NULL, "\n", &preservedEvent);

      if (_printDebug == true)
        _debugPort->println(F("checkUnsolicitedMsg: end of event")); //Just to denote end of processing event.

      if (event == NULL)
        if (_printDebug == true)
          _debugPort->println(F("checkUnsolicitedMsg: <=== end of event(s)!"));
    }
  }

  swarm_m138_free_char(event);

  _checkUnsolicitedMsgReentrant = false;

  return handled;
} // /checkUnsolicitedMsg

// Parse incoming unsolicited messages - pass the data to the user via the callbacks (if defined)
bool SWARM_M138::processUnsolicitedEvent(const char *event)
{
  { // $DT - Date/Time
    Swarm_M138_DateTimeData_t *dateTime = new Swarm_M138_DateTimeData_t;
    char *eventStart;
    char *eventEnd;

    eventStart = strstr(event, "$DT ");
    if (eventStart != NULL)
    {
      eventEnd = strchr(eventStart, '*'); // Stop at the asterix
      if (eventEnd != NULL)
      {
        if (eventEnd >= (eventStart + 20)) // Check we have enough data
        {
          // Extract the Date, Time and flag
          char c;
          eventStart += 4; // Point at the first digit of the year

          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = (uint16_t)(c - '0') * 1000; // Get the year
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = dateTime->YYYY + ((uint16_t)(c - '0') * 100);
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = dateTime->YYYY + ((uint16_t)(c - '0') * 10);
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = dateTime->YYYY + ((uint16_t)(c - '0') * 1);

          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->MM = (uint8_t)(c - '0') * 10; // Get the month
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->MM = dateTime->MM + ((uint8_t)(c - '0') * 1);

          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->DD = (uint8_t)(c - '0') * 10; // Get the day of month
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->DD = dateTime->DD + ((uint8_t)(c - '0') * 1);

          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->hh = (uint8_t)(c - '0') * 10; // Get the hour
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->hh = dateTime->hh + ((uint8_t)(c - '0') * 1);

          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->mm = (uint8_t)(c - '0') * 10; // Get the minute
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->mm = dateTime->mm + ((uint8_t)(c - '0') * 1);

          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->ss = (uint8_t)(c - '0') * 10; // Get the second
          c = *eventStart; eventStart++; if ((c >= '0') && (c <= '9')) dateTime->ss = dateTime->ss + ((uint8_t)(c - '0') * 1);

          eventStart++;
          dateTime->valid = *eventStart == 'V'; // Get the flag. Convert to bool

          if (_swarmDateTimeCallback != NULL)
          {
            _swarmDateTimeCallback((const Swarm_M138_DateTimeData_t *)dateTime); // Call the callback
          }

          delete dateTime;
          return (true);
        }
      }
    }
    delete dateTime;
  }
  // { // URC: +UULOC (Localization information - CellLocate and hybrid positioning)
  //   ClockData clck;
  //   PositionData gps;
  //   SpeedData spd;
  //   unsigned long uncertainty;
  //   int scanNum;
  //   int latH, lonH, alt;
  //   unsigned int speedU, cogU;
  //   char latL[10], lonL[10];
  //   int dateStore[5];

  //   // Maybe we should also scan for +UUGIND and extract the activated gnss system?

  //   // This assumes the ULOC response type is "0" or "1" - as selected by gpsRequest detailed
  //   scanNum = sscanf(event,
  //                     "+UULOC: %d/%d/%d,%d:%d:%d.%d,%d.%[^,],%d.%[^,],%d,%lu,%u,%u,%*s",
  //                     &dateStore[0], &dateStore[1], &clck.date.year,
  //                     &dateStore[2], &dateStore[3], &dateStore[4], &clck.time.ms,
  //                     &latH, latL, &lonH, lonL, &alt, &uncertainty,
  //                     &speedU, &cogU);
  //   clck.date.day = dateStore[0];
  //   clck.date.month = dateStore[1];
  //   clck.time.hour = dateStore[2];
  //   clck.time.minute = dateStore[3];
  //   clck.time.second = dateStore[4];

  //   if (scanNum >= 13)
  //   {
  //     // Found a Location string!
  //     if (_printDebug == true)
  //     {
  //       _debugPort->println(F("processReadEvent: location"));
  //     }

  //     if (latH >= 0)
  //       gps.lat = (float)latH + ((float)atol(latL) / pow(10, strlen(latL)));
  //     else
  //       gps.lat = (float)latH - ((float)atol(latL) / pow(10, strlen(latL)));
  //     if (lonH >= 0)
  //       gps.lon = (float)lonH + ((float)atol(lonL) / pow(10, strlen(lonL)));
  //     else
  //       gps.lon = (float)lonH - ((float)atol(lonL) / pow(10, strlen(lonL)));
  //     gps.alt = (float)alt;
  //     if (scanNum >= 15) // If detailed response, get speed data
  //     {
  //       spd.speed = (float)speedU;
  //       spd.cog = (float)cogU;
  //     }

  //     // if (_printDebug == true)
  //     // {
  //     //   _debugPort->print(F("processReadEvent: location:  lat: "));
  //     //   _debugPort->print(gps.lat, 7);
  //     //   _debugPort->print(F(" lon: "));
  //     //   _debugPort->print(gps.lon, 7);
  //     //   _debugPort->print(F(" alt: "));
  //     //   _debugPort->print(gps.alt, 2);
  //     //   _debugPort->print(F(" speed: "));
  //     //   _debugPort->print(spd.speed, 2);
  //     //   _debugPort->print(F(" cog: "));
  //     //   _debugPort->println(spd.cog, 2);
  //     // }

  //     if (_gpsRequestCallback != NULL)
  //     {
  //       _gpsRequestCallback(clck, gps, spd, uncertainty);
  //     }

  //     return true;
  //   }
  // }
  return false;
} // /processUnsolicitedEvent

/**************************************************************************/
/*!
    @brief  Read the modem device ID and name using the $CS message
    @param  settings
            A pointer to where the settings will be stored
            It is recommended that the user allocates at least
            SWARM_M138_MEM_ALLOC_CS bytes to store the settings
    @return SWARM_M138_ERROR_SUCCESS if successful
            SWARM_M138_ERROR_MEM_ALLOC if the memory allocation fails
            SWARM_M138_ERROR_ERROR if unsuccessful
*/
/**************************************************************************/
// Only 25 bytes are needed to store the reply,
// but an unsolicited receive data message could arrive while we are waiting for the response...
Swarm_M138_Error_e SWARM_M138::getConfigurationSettings(char *settings)
{
  char *command;
  char *response;
  char *responseStart;
  char *responseEnd;
  Swarm_M138_Error_e err;

  // Allocate memory for the command, asterix, checksum bytes, \n and \0
  command = swarm_m138_alloc_char(strlen(SWARM_M138_COMMAND_CONFIGURATION) + 5);
  if (command == NULL)
    return (SWARM_M138_ERROR_MEM_ALLOC);
  memset(command, 0, strlen(SWARM_M138_COMMAND_CONFIGURATION) + 5); // Clear it
  sprintf(command, "%s*", SWARM_M138_COMMAND_CONFIGURATION); // Copy the command, add the asterix
  addChecksumLF(command); // Add the checksum bytes and line feed

  response = swarm_m138_alloc_char(_RxBuffSize); // Allocate memory for the response
  if (response == NULL)
  {
    swarm_m138_free_char(command);
    return(SWARM_M138_ERROR_MEM_ALLOC);
  }
  memset(response, 0, _RxBuffSize); // Clear it

  err = sendCommandWithResponse(command, "$CS DI=0x", response, _RxBuffSize);

  if (err == SWARM_M138_ERROR_SUCCESS)
  {
    responseStart = strstr(response, "$CS DI=0x");
    if (responseStart == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }
    responseEnd = strchr(responseStart, '*'); // Stop at the asterix
    if (responseEnd == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }

    // Copy the response to *settings
    responseStart += 4; // Start at the 'D'
    memcpy(settings, responseStart, responseEnd - responseStart);

    // Add a null-terminator
    settings[responseEnd - responseStart] = 0;
  }

  swarm_m138_free_char(command);
  swarm_m138_free_char(response);
  return (err);
}

/**************************************************************************/
/*!
    @brief  Read the modem device ID using the $CS message
    @param  id
            The address of the uint32_t where the ID will be stored
    @return SWARM_M138_ERROR_SUCCESS if successful
            SWARM_M138_ERROR_MEM_ALLOC if the memory allocation fails
            SWARM_M138_ERROR_ERROR if unsuccessful
*/
/**************************************************************************/
// Only ~25 bytes are needed to store the reply,
// but an unsolicited receive data message _could_ arrive while we are waiting for the response...
// So we need to allocate the full _RxBuffSize for the response.
Swarm_M138_Error_e SWARM_M138::getDeviceID(uint32_t *id)
{
  char *command;
  char *response;
  char *responseStart;
  char *responseEnd;
  Swarm_M138_Error_e err;
  uint32_t dev_ID = 0;

  // Allocate memory for the command, asterix, checksum bytes, \n and \0
  command = swarm_m138_alloc_char(strlen(SWARM_M138_COMMAND_CONFIGURATION) + 5);
  if (command == NULL)
    return (SWARM_M138_ERROR_MEM_ALLOC);
  memset(command, 0, strlen(SWARM_M138_COMMAND_CONFIGURATION) + 5); // Clear it
  sprintf(command, "%s*", SWARM_M138_COMMAND_CONFIGURATION); // Copy the command, add the asterix
  addChecksumLF(command); // Add the checksum bytes and line feed

  response = swarm_m138_alloc_char(_RxBuffSize); // Allocate memory for the response
  if (response == NULL)
  {
    swarm_m138_free_char(command);
    return(SWARM_M138_ERROR_MEM_ALLOC);
  }
  memset(response, 0, _RxBuffSize); // Clear it

  err = sendCommandWithResponse(command, "$CS DI=0x", response, _RxBuffSize);

  if (err == SWARM_M138_ERROR_SUCCESS)
  {
    responseStart = strstr(response, "$CS DI=0x");
    if (responseStart == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }
    responseEnd = strchr(responseStart, ','); // Stop at the comma
    if (responseEnd == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }

    // Extract the ID
    responseStart += 9; // Point at the first digit
    while (responseStart < responseEnd)
    {
      dev_ID <<= 4; // Shuffle the existing value along by 4 bits
      char c = *responseStart; // Get the digit
      if ((c >= '0') && (c <= '9'))
        dev_ID |= c - '0';
      else if ((c >= 'a') && (c <= 'f'))
        dev_ID |= c + 10 - 'a';
      else if ((c >= 'A') && (c <= 'F'))
        dev_ID |= c + 10 - 'A';
      responseStart++;
    }

    if (_printDebug == true)
    {
      _debugPort->print(F("getDeviceID: dev_ID is 0x"));
      _debugPort->println(dev_ID, HEX);
    }

    *id = dev_ID; // Copy the extracted ID into id
  }

  swarm_m138_free_char(command);
  swarm_m138_free_char(response);
  return (err);
}

/**************************************************************************/
/*!
    @brief  Get the most recent $DT message
    @param  dateTime
            A pointer to a Swarm_M138_DateTimeData_t struct which will hold the result
    @return SWARM_M138_ERROR_SUCCESS if successful
            SWARM_M138_ERROR_MEM_ALLOC if the memory allocation fails
            SWARM_M138_ERROR_ERROR if unsuccessful
*/
/**************************************************************************/
Swarm_M138_Error_e SWARM_M138::getDateTime(Swarm_M138_DateTimeData_t *dateTime)
{
  char *command;
  char *response;
  char *responseStart;
  char *responseEnd;
  Swarm_M138_Error_e err;

  // Allocate memory for the command, asterix, checksum bytes, \n and \0
  command = swarm_m138_alloc_char(strlen(SWARM_M138_COMMAND_DATE_TIME_STAT) + 7);
  if (command == NULL)
    return (SWARM_M138_ERROR_MEM_ALLOC);
  memset(command, 0, strlen(SWARM_M138_COMMAND_DATE_TIME_STAT) + 7); // Clear it
  sprintf(command, "%s @*", SWARM_M138_COMMAND_DATE_TIME_STAT); // Copy the command, add the asterix
  addChecksumLF(command); // Add the checksum bytes and line feed

  response = swarm_m138_alloc_char(_RxBuffSize); // Allocate memory for the response
  if (response == NULL)
  {
    swarm_m138_free_char(command);
    return(SWARM_M138_ERROR_MEM_ALLOC);
  }
  memset(response, 0, _RxBuffSize); // Clear it

  err = sendCommandWithResponse(command, "$DT ", response, _RxBuffSize);

  if (err == SWARM_M138_ERROR_SUCCESS)
  {
    responseStart = strstr(response, "$DT ");
    if (responseStart == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }
    responseEnd = strchr(responseStart, '*'); // Stop at the asterix
    if (responseEnd == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }

    if (responseEnd < (responseStart + 20)) // Check we have enough data
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }

    // Extract the Date, Time and flag
    char c;
    responseStart += 4; // Point at the first digit of the year

    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = (uint16_t)(c - '0') * 1000; // Get the year
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = dateTime->YYYY + ((uint16_t)(c - '0') * 100);
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = dateTime->YYYY + ((uint16_t)(c - '0') * 10);
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->YYYY = dateTime->YYYY + ((uint16_t)(c - '0') * 1);

    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->MM = (uint8_t)(c - '0') * 10; // Get the month
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->MM = dateTime->MM + ((uint8_t)(c - '0') * 1);

    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->DD = (uint8_t)(c - '0') * 10; // Get the day of month
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->DD = dateTime->DD + ((uint8_t)(c - '0') * 1);

    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->hh = (uint8_t)(c - '0') * 10; // Get the hour
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->hh = dateTime->hh + ((uint8_t)(c - '0') * 1);

    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->mm = (uint8_t)(c - '0') * 10; // Get the minute
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->mm = dateTime->mm + ((uint8_t)(c - '0') * 1);

    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->ss = (uint8_t)(c - '0') * 10; // Get the second
    c = *responseStart; responseStart++; if ((c >= '0') && (c <= '9')) dateTime->ss = dateTime->ss + ((uint8_t)(c - '0') * 1);

    responseStart++;
    dateTime->valid = *responseStart == 'V'; // Get the flag. Convert to bool
  }

  swarm_m138_free_char(command);
  swarm_m138_free_char(response);
  return (err);
}

/**************************************************************************/
/*!
    @brief  Query the current $DT rate
    @param  rate
            A pointer to a uint32_t which will hold the result
    @return SWARM_M138_ERROR_SUCCESS if successful
            SWARM_M138_ERROR_MEM_ALLOC if the memory allocation fails
            SWARM_M138_ERROR_ERROR if unsuccessful
*/
/**************************************************************************/
Swarm_M138_Error_e SWARM_M138::getDateTimeRate(uint32_t *rate)
{
  char *command;
  char *response;
  char *responseStart;
  char *responseEnd;
  Swarm_M138_Error_e err;

  // Allocate memory for the command, asterix, checksum bytes, \n and \0
  command = swarm_m138_alloc_char(strlen(SWARM_M138_COMMAND_DATE_TIME_STAT) + 7);
  if (command == NULL)
    return (SWARM_M138_ERROR_MEM_ALLOC);
  memset(command, 0, strlen(SWARM_M138_COMMAND_DATE_TIME_STAT) + 7); // Clear it
  sprintf(command, "%s ?*", SWARM_M138_COMMAND_DATE_TIME_STAT); // Copy the command, add the asterix
  addChecksumLF(command); // Add the checksum bytes and line feed

  response = swarm_m138_alloc_char(_RxBuffSize); // Allocate memory for the response
  if (response == NULL)
  {
    swarm_m138_free_char(command);
    return(SWARM_M138_ERROR_MEM_ALLOC);
  }
  memset(response, 0, _RxBuffSize); // Clear it

  err = sendCommandWithResponse(command, "$DT ", response, _RxBuffSize);

  if (err == SWARM_M138_ERROR_SUCCESS)
  {
    responseStart = strstr(response, "$DT ");
    if (responseStart == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }
    responseEnd = strchr(responseStart, '*'); // Stop at the asterix
    if (responseEnd == NULL)
    {
      swarm_m138_free_char(command);
      swarm_m138_free_char(response);
      return (SWARM_M138_ERROR_ERROR);
    }

    // Extract the rate
    char c;
    uint32_t theRate = 0;
    responseStart += 4; // Point at the first digit of the rate

    c = *responseStart; // Get the first digit of the rate
    while (c != '*') // Keep going until we hit the asterix
    {
      if ((c >= '0') && (c <= '9')) // Extract the rate one digit at a time
      {
        theRate = theRate * 10;
        theRate += (uint32_t)(c - '0');
      }
      responseStart++;
      c = *responseStart; // Get the next digit of the rate
    }

    *rate = theRate;
  }

  swarm_m138_free_char(command);
  swarm_m138_free_char(response);
  return (err);
}


/**************************************************************************/
/*!
    @brief  Set the rate of $DT Date/Time messages
    @param  rate
            The interval between messages
            0 == Disable. Max is 2147483647 (2^31 - 1)
    @return SWARM_M138_ERROR_SUCCESS if successful
            SWARM_M138_ERROR_INVALID_RATE if the rate is invalid
            SWARM_M138_ERROR_MEM_ALLOC if the memory allocation fails
            SWARM_M138_ERROR_ERR if a command ERR is received - error is returned in commandError
            SWARM_M138_ERROR_ERROR if unsuccessful
*/
/**************************************************************************/
Swarm_M138_Error_e SWARM_M138::setDateTimeRate(uint32_t rate)
{
  char *command;
  char *response;
  Swarm_M138_Error_e err;

  // Check rate is within bounds
  if (rate > SWARM_M138_MAX_MESSAGE_RATE)
    return (SWARM_M138_ERROR_INVALID_RATE);

  // Allocate memory for the command, rate, asterix, checksum bytes, \n and \0
  command = swarm_m138_alloc_char(strlen(SWARM_M138_COMMAND_DATE_TIME_STAT) + 1 + 10 + 5);
  if (command == NULL)
    return (SWARM_M138_ERROR_MEM_ALLOC);
  memset(command, 0, strlen(SWARM_M138_COMMAND_DATE_TIME_STAT) + 1 + 10 + 5); // Clear it
  sprintf(command, "%s %u*", SWARM_M138_COMMAND_DATE_TIME_STAT, rate); // Copy the command, add the asterix
  addChecksumLF(command); // Add the checksum bytes and line feed

  response = swarm_m138_alloc_char(_RxBuffSize); // Allocate memory for the response
  if (response == NULL)
  {
    swarm_m138_free_char(command);
    return(SWARM_M138_ERROR_MEM_ALLOC);
  }
  memset(response, 0, _RxBuffSize); // Clear it

  sendCommand(command); // Send the command

  err = waitForResponse("$DT OK*", "$DT ERR");

  swarm_m138_free_char(command);
  swarm_m138_free_char(response);
  return (err);
}

/**************************************************************************/
/*!
    @brief  Set up the callback for the $DT Date Time message
    @param  _swarmDateTimeCallback
            The address of the function to be called when an unsolicited $DT message arrives
*/
/**************************************************************************/
void SWARM_M138::setDateTimeCallback(void (*swarmDateTimeCallback)(const Swarm_M138_DateTimeData_t *dateTime))
{
  _swarmDateTimeCallback = swarmDateTimeCallback;
}

/**************************************************************************/
/*!
    @brief  Convert modem status enum into printable text
    @param  status
            The modem status (enumerated)
    @return A pointer to the modem status in string (const char) format
*/
/**************************************************************************/
const char *SWARM_M138::modemStatusString(Swarm_M138_Modem_Status_e status)
{
  switch (status)
  {
    case SWARM_M138_MODEM_STATUS_BOOT_ABORT:
      return "BOOT ABORT (Restart after firmware crash)";
      break;
    case SWARM_M138_MODEM_STATUS_BOOT_POWERON:
      return "BOOT POWERON (Power has been applied)";
      break;
    case SWARM_M138_MODEM_STATUS_BOOT_RUNNING:
      return "BOOT RUNNING (Boot has completed. Ready to accept commands)";
      break;
    case SWARM_M138_MODEM_STATUS_BOOT_UPDATED:
      return "BOOT UPDATED (A firmware update was performed)";
      break;
    case SWARM_M138_MODEM_STATUS_BOOT_VERSION:
      return "BOOT VERSION (Firmware version)";
      break;
    case SWARM_M138_MODEM_STATUS_DATETIME:
      return "DATETIME (GPS has acquired a valid date/time reference)";
      break;
    case SWARM_M138_MODEM_STATUS_POSITION:
      return "POSITION (GPS has acquired a valid position 3D fix)";
      break;
    case SWARM_M138_MODEM_STATUS_DEBUG:
      return "DEBUG (Debug message): ";
      break;
    case SWARM_M138_MODEM_STATUS_ERROR:
      return "ERROR (Error message): ";
      break;
  }
  return "UNKNOWN";
}

/**************************************************************************/
/*!
    @brief  Convert modem error enum into printable text
    @param  error
            The modem error (enumerated)
    @return A pointer to the modem error in string (const char) format
*/
/**************************************************************************/
const char *SWARM_M138::modemErrorString(Swarm_M138_Error_e error)
{
  switch (error)
  {
    case SWARM_M138_ERROR_ERROR:
      return "Just a plain old communication error";
      break;
    case SWARM_M138_ERROR_SUCCESS:
      return "Hey, it worked!";
      break;
    case SWARM_M138_ERROR_MEM_ALLOC:
      return "Memory allocation error";
      break;
    case SWARM_M138_ERROR_TIMEOUT:
      return "Communication timeout";
      break;
    case SWARM_M138_ERROR_INVALID_FORMAT:
      return "Indicates the command response format was invalid: missing $ or *; non-ASCII checksum";
      break;
    case SWARM_M138_ERROR_INVALID_CHECKSUM:
      return "Indicates the command response checksum was invalid";
      break;
    case SWARM_M138_ERROR_INVALID_RATE:
      return "The requested message rate was invalid";
      break;
    case SWARM_M138_ERROR_ERR:
      return "Command input error (ERR)";
      break;
  }

  return "UNKNOWN";
}

/**************************************************************************/
/*!
    @brief  Convert command error into a printable description
    @param  ERR
            The commad error as const char *
    @return A pointer to the command error description in string (const char) format
*/
/**************************************************************************/
const char *SWARM_M138::commandErrorString(const char *ERR)
{
  if (strstr(ERR, "BADPARAM") != NULL)
    return "Invalid command or argument";
  if (strstr(ERR, "DBXINVMSGID") != NULL)
    return "Messages Management : invalid message ID";
  if (strstr(ERR, "DBXNOMORE") != NULL)
    return "Messages Management : no messages found";
  if (strstr(ERR, "TIMENOTSET") != NULL)
    return "Time not yet set from GPS";
  if (strstr(ERR, "NOCOMMAND") != NULL)
    return "Sleep Mode : No S or U partameter";
  if (strstr(ERR, "NOTIME") != NULL)
    return "Sleep Mode : attempt to sleep before time is set";
  if (strstr(ERR, "BADAPPID") != NULL)
    return "Transmit Data : invalid application ID";
  if (strstr(ERR, "BADDATA") != NULL)
    return "Transmit Data : Message has odd number or non-hex characters when sending data as hexadecimal";
  if (strstr(ERR, "BADEXPIRETIME") != NULL)
    return "Transmit Data : Invalid hold time";
  if (strstr(ERR, "HOLDTIMEEXPIRED") != NULL)
    return "Transmit Data : Unable to send within requested hold time";
  if (strstr(ERR, "NODEVICEID") != NULL)
    return "Transmit Data : The Swarm device ID has not yet been set - contact Swarm Support";
  if (strstr(ERR, "NOSPACE") != NULL)
    return "Transmit Data : No space for message";
  if (strstr(ERR, "DBXTOHIVEFULL") != NULL)
    return "Transmit Data : Queue for queued messages is full. Maximum of 2048 messages may be held in the queue";
  if (strstr(ERR, "TOOLONG") != NULL)
    return "Transmit Data : Message is too large to send";

  return "UNKNOWN";
}

/////////////
// Private //
/////////////

// Add the two NMEA checksum bytes and line feed to a command
void SWARM_M138::addChecksumLF(char *command)
{
  char *dollar = strchr(command, '$'); // Find the $

  if (dollar == NULL) // Return now if the $ was not found
    return;

  char *asterix = strchr(dollar, '*'); // Find the *

  if (asterix == NULL) // Return now if the * was not found
    return;

  char checksum = 0;

  dollar++; // Point to the char after the $

  while (dollar < asterix) // Calculate the checksum
  {
    checksum ^= *dollar;
    dollar++;
  }

  // Add the checksum bytes to the command
  *(asterix + 1) = (checksum >> 4) + '0';
  if (*(asterix + 1) >= ':') // Hex a-f
    *(asterix + 1) = *(asterix + 1) + 'a' - ':';
  *(asterix + 2) = (checksum & 0x0F) + '0';
  if (*(asterix + 2) >= ':') // Hex a-f
    *(asterix + 2) = *(asterix + 2) + 'a' - ':';

  // Add the line feed
  *(asterix + 3) = '\n';

  // Add a \0 - just in case
  *(asterix + 4) = 0;
}

// Check if the response / message checksum is valid
Swarm_M138_Error_e SWARM_M138::checkChecksum(char *startPosition)
{
  char *dollar = strchr(startPosition, '$'); // Find the $

  if (dollar == NULL) // Return now if the $ was not found
  {
    if (_printDebug == true)
    {
      _debugPort->println(F("checkChecksum: $ not found!"));
    }
    return (SWARM_M138_ERROR_INVALID_FORMAT);
  }

  char *asterix = strchr(dollar, '*'); // Find the *

  if (asterix == NULL) // Return now if the * was not found
  {
    if (_printDebug == true)
    {
      _debugPort->println(F("checkChecksum: * not found!"));
    }
    return (SWARM_M138_ERROR_INVALID_FORMAT);
  }

  char checksum = 0;

  dollar++; // Point to the char after the $

  while (dollar < asterix) // Calculate the checksum
  {
    checksum ^= *dollar;
    dollar++;
  }

  char expectedChecksum;

  char checksumChar = *(asterix + 1); // Get the first checksum character

  if ((checksumChar >= '0') && (checksumChar <= '9')) // Convert to binary
    expectedChecksum = (checksumChar - '0') << 4;
  else if ((checksumChar >= 'a') && (checksumChar <= 'f'))
    expectedChecksum = (checksumChar + 10 - 'a') << 4;
  else if ((checksumChar >= 'A') && (checksumChar <= 'F'))
    expectedChecksum = (checksumChar + 10 - 'A') << 4;
  else
  {
    if (_printDebug == true)
    {
      _debugPort->println(F("checkChecksum: invalid checksum char 1"));
    }
    return (SWARM_M138_ERROR_INVALID_FORMAT);
  }

  checksumChar = *(asterix + 2); // Get the second checksum character

  if ((checksumChar >= '0') && (checksumChar <= '9')) // Convert to binary
    expectedChecksum |= (checksumChar - '0');
  else if ((checksumChar >= 'a') && (checksumChar <= 'f'))
    expectedChecksum |= (checksumChar + 10 - 'a');
  else if ((checksumChar >= 'A') && (checksumChar <= 'F'))
    expectedChecksum |= (checksumChar + 10 - 'A');
  else
    return (SWARM_M138_ERROR_INVALID_FORMAT);

  if (checksum != expectedChecksum)
  {
    if (_printDebug == true)
    {
      _debugPort->println(F("checkChecksum: invalid checksum char 2"));
    }
    return (SWARM_M138_ERROR_INVALID_CHECKSUM);
  }

  return (SWARM_M138_ERROR_SUCCESS);
}

// Extract the command error
Swarm_M138_Error_e SWARM_M138::extractCommandError(char *startPosition)
{
    memset(commandError, 0, SWARM_M138_MAX_CMD_ERROR_LEN); // Clear any existing error

    char *errorAt = strstr(startPosition, "ERR,"); // Find the ERR,

    if (errorAt == NULL)
      return (SWARM_M138_ERROR_ERROR);

    errorAt += 4; // Point to the start of the actual error message

    char *asterix = strchr(errorAt, '*'); // Find the *

    if (asterix == NULL)
      return (SWARM_M138_ERROR_ERROR);

    int errorLen = 0;
    while ((errorAt < asterix) && (errorLen < (SWARM_M138_MAX_CMD_ERROR_LEN - 1))) // Leave a NULL on the end
    {
      commandError[errorLen] = *errorAt;
      errorAt++;
      errorLen++;
    }

    return (SWARM_M138_ERROR_SUCCESS);
}

// Send a command. Check for a response.
// Return true if expectedResponseStart is seen in the data followed by a \n
Swarm_M138_Error_e SWARM_M138::sendCommandWithResponse(
    const char *command, const char *expectedResponseStart, char *responseDest,
    int destSize, unsigned long commandTimeout)
{
  bool found = false;
  bool responseStartSeen = false;
  int index = 0;
  int destIndex = 0;
  size_t responseStartedAt = 0;

  bool printedSomething = false;

  if (_printDebug == true)
  {
    _debugPort->print(F("sendCommandWithResponse: Command: "));
    _debugPort->println(command);
  }

  sendCommand(command); //Sending command needs to dump data to backlog buffer as well.
  unsigned long timeIn = millis();

  while ((!found) && ((timeIn + commandTimeout) > millis()))
  {
    int hwAvail = hwAvailable();
    if (hwAvail > 0) //hwAvailable can return -1 if the serial port is NULL
    {
      if ((destIndex + hwAvail) <= destSize) // Check there is room to store the response
      {
        int bytesRead = hwReadChars((char *)&responseDest[destIndex], hwAvail);

        if (_printDebug == true)
        {
          if (printedSomething == false)
          {
            _debugPort->print(F("sendCommandWithResponse: Response: "));
            printedSomething = true;
          }
          _debugPort->print((const char *)&responseDest[destIndex]);
        }
        
        // Check each character to see if it is the expected resonse or error
        for (size_t chrPtr = destIndex; chrPtr < (destIndex + bytesRead); chrPtr++)
        {
          char c = responseDest[chrPtr]; // Check each character
          if (c == expectedResponseStart[index])
          {
            if ((index == 0)  && (responseStartSeen == false))
              responseStartedAt = chrPtr; // Record where the (possible) response started
            if (++index == (int)strlen(expectedResponseStart))
            {
              responseStartSeen = true;
            }
          }
          else
          {
            index = 0;
          }
          if ((responseStartSeen) && (c == '\n'))
            found = true;
        }

        // Now also copy the response into the backlog, if there is room
        size_t backlogLength = strlen((const char *)_swarmBacklog);

        if ((backlogLength + bytesRead) <= _RxBuffSize) // Is there room to store the new data?
        {
          memcpy((char *)&_swarmBacklog[backlogLength], (char *)&responseDest[destIndex], bytesRead);
        }
        else
        {
          if (_printDebug == true)
          {
            if (printedSomething == true)
            {
              _debugPort->println();
              printedSomething = false;
            }
            _debugPort->println(F("sendCommandWithResponse: Panic! _swarmBacklog is full!"));
          }
        }

        destIndex += bytesRead; // Increment destIndex
      }
      else
      {
        if (_printDebug == true)
        {
          if (printedSomething == true)
          {
            _debugPort->println();
            printedSomething = false;
          }
          _debugPort->println(F("sendCommandWithResponse: Panic! responseDest is full!"));
        }
      }
    }
  }

  if (_printDebug == true)
    if (printedSomething)
      _debugPort->println();

  pruneBacklog(); // Prune any incoming non-actionable URC's and responses/errors from the backlog

  if (found) // Check the NMEA checksum is valid
  {
    return (checkChecksum((char *)&responseDest[responseStartedAt]));
  }

  return SWARM_M138_ERROR_TIMEOUT;
}

void SWARM_M138::sendCommand(const char *command)
{
  //Spend up to _rxWindowMillis milliseconds copying any incoming serial data into the backlog
  unsigned long timeIn = millis();
  int hwAvail = hwAvailable();
  if (hwAvail > 0) //hwAvailable can return -1 if the serial port is NULL
  {
    size_t backlogLength = strlen((const char *)_swarmBacklog);
    while (((millis() - timeIn) < _rxWindowMillis) && ((backlogLength + hwAvail) <= _RxBuffSize)) //May need to escape on newline?
    {
      if (hwAvail > 0) //hwAvailable can return -1 if the serial port is NULL
      {
        backlogLength += hwReadChars((char *)&_swarmBacklog[backlogLength], hwAvail);
        timeIn = millis();
      }
      hwAvail = hwAvailable();
    }
  }

  if (_printDebug == true)
  {
    _debugPort->print(F("sendCommand: Command: "));
    _debugPort->println(command);
  }

  //Now send the command
  hwPrint(command);
}

Swarm_M138_Error_e SWARM_M138::waitForResponse(const char *expectedResponseStart, const char *expectedErrorStart, unsigned long timeout)
{
  unsigned long timeIn;
  bool found = false;
  bool responseStartSeen = false, errorStartSeen = false;
  int responseIndex = 0, errorIndex = 0;
  size_t responseStartedAt = 0, errorStartedAt = 0;
  Swarm_M138_Error_e err = SWARM_M138_ERROR_ERROR;

  bool printedSomething = false;

  timeIn = millis();

  while ((!found) && ((timeIn + timeout) > millis()))
  {
    int hwAvail = hwAvailable();
    if (hwAvail > 0) //hwAvailable can return -1 if the serial port is NULL
    {
      // Store everything in the backlog - if there is room
      // _swarmBacklog is a global array that holds the backlog of any events
      // that came in while waiting for response. To be processed later within checkUnsolicitedMsg().
      // Note: the expectedResponse or expectedError will also be added to the backlog.
      // Everything in the backlog is 'printable'. So it is OK to use strlen.
      size_t backlogLength = strlen((const char *)_swarmBacklog);

      if ((backlogLength + hwAvail) <= _RxBuffSize) // Is there room to store the new data?
      {
        hwReadChars((char *)&_swarmBacklog[backlogLength], hwAvail);

        if (_printDebug == true)
        {
          if (printedSomething == false)
          {
            _debugPort->print(F("waitForResponse: "));
            printedSomething = true;
          }
          _debugPort->print((const char *)&_swarmBacklog[backlogLength]);
        }

        // Check each character to see if it is the expected resonse or error
        for (size_t chrPtr = backlogLength; chrPtr < (backlogLength + hwAvail); chrPtr++)
        {
          char c = _swarmBacklog[chrPtr]; // Check each character

          if (c == expectedResponseStart[responseIndex])
          {
            if ((responseIndex == 0) && (responseStartSeen == false))
              responseStartedAt = chrPtr; // Record where the (possible) response started
            if (++responseIndex == (int)strlen(expectedResponseStart))
            {
              responseStartSeen = true;
            }
          }
          else
          {
            responseIndex = 0;
          }

          if (expectedErrorStart != NULL)
          {
            if (c == expectedErrorStart[errorIndex])
            {
              if ((errorIndex == 0) && (errorStartSeen == false))
                errorStartedAt = chrPtr; // Record where the (possible) error started
              if (++errorIndex == (int)strlen(expectedErrorStart))
              {
                errorStartSeen = true;
              }
            }
            else
            {
              errorIndex = 0;
            }
          }

          if ((responseStartSeen || errorStartSeen) && (c == '\n'))
            found = true;
        }
      }
      else
      {
        if (_printDebug == true)
        {
          if (printedSomething == true)
          {
            _debugPort->println();
            printedSomething = false;
          }
          _debugPort->println(F("waitForResponse: Panic! _swarmBacklog is full!"));
        }
      }
    }
  }

  if (_printDebug == true)
    if (printedSomething)
      _debugPort->println();

  if (found == true)
  {
    if (responseStartSeen) // Let success have priority
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("waitForResponse: responseStart: "));
        _debugPort->println((char *)&_swarmBacklog[responseStartedAt]);
      }
      err = checkChecksum((char *)&_swarmBacklog[responseStartedAt]);
    }
    else if (errorStartSeen)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("waitForResponse: errorStart: "));
        _debugPort->println((char *)&_swarmBacklog[errorStartedAt]);
      }
      err = checkChecksum((char *)&_swarmBacklog[errorStartedAt]);
      if (err == SWARM_M138_ERROR_SUCCESS)
      {
        extractCommandError((char *)&_swarmBacklog[errorStartedAt]);
        err = SWARM_M138_ERROR_ERR;
      }
    }
  }
  else
    err = SWARM_M138_ERROR_TIMEOUT;

  pruneBacklog(); // Prune any incoming non-actionable URC's and responses/errors from the backlog

  return (err);
}

size_t SWARM_M138::hwPrint(const char *s)
{
  if (_hardSerial != NULL)
  {
    return _hardSerial->print(s);
  }
#ifdef SARA_R5_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != NULL)
  {
    return _softSerial->print(s);
  }
#endif
  else if (_i2cPort != NULL)
  {
    return ((size_t)qwiicSwarmWriteChars(strlen(s), s));
  }

  return (size_t)0;
}

size_t SWARM_M138::hwWriteData(const char *buff, int len)
{
  if (_hardSerial != NULL)
  {
    return _hardSerial->write((const uint8_t *)buff, len);
  }
#ifdef SWARM_M138_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != NULL)
  {
    return _softSerial->write((const uint8_t *)buff, len);
  }
#endif
  else if (_i2cPort != NULL)
  {
    return ((size_t)qwiicSwarmWriteChars(len, buff));
  }

  return (size_t)0;
}

size_t SWARM_M138::hwWrite(const char c)
{
  if (_hardSerial != NULL)
  {
    return _hardSerial->write(c);
  }
#ifdef SWARM_M138_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != NULL)
  {
    return _softSerial->write(c);
  }
#endif
  else if (_i2cPort != NULL)
  {
    return ((size_t)qwiicSwarmWriteChars((int)1, &c));
  }

  return (size_t)0;
}

int SWARM_M138::hwAvailable(void)
{
  if (_hardSerial != NULL)
  {
    return ((int)_hardSerial->available());
  }
#ifdef SWARM_M138_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != NULL)
  {
    return ((int)_softSerial->available());
  }
#endif
  else if (_i2cPort != NULL)
  {
    return (qwiicSwarmAvailable());
  }

  return -1;
}

// Read len chars from the appropriate port. Store in buf
int SWARM_M138::hwReadChars(char *buf, int len)
{
  if (len <= 0)
    return (len);

  if (buf == NULL)
    return (-1);

  if (_hardSerial != NULL)
  {
    for (int i = 0; i < len; i++)
    {
      buf[i] = _hardSerial->read();
    }
    return (len);
  }
#ifdef SWARM_M138_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != NULL)
  {
    for (int i = 0; i < len; i++)
    {
      buf[i] = _softSerial->read();
    }
    return (len);
  }
#endif
  else if (_i2cPort != NULL)
  {
    return (qwiicSwarmReadChars(len, buf));
  }

  return (-1);

}

// I2C functions for Qwiic Swarm

// Check how many bytes Qwiic Swarm has available
// Return -1 if it is less than QWIIC_SWARM_I2C_POLLING_WAIT_MS since the last check
int SWARM_M138::qwiicSwarmAvailable(void)
{
  int bytesAvailable = -1;

  if (millis() - _lastI2cCheck >= QWIIC_SWARM_I2C_POLLING_WAIT_MS)
  {
    //Check how many serial bytes are waiting to be read
    _i2cPort->beginTransmission((uint8_t)_address); // Talk to the I2C device
    _i2cPort->write(QWIIC_SWARM_LEN_REG); // Point to the serial buffer length
    _i2cPort->endTransmission(); // Send data and release the bus (the 841 (WireS) doesn't like it if the Controller holds the bus!)
    if (_i2cPort->requestFrom((uint8_t)_address, (uint8_t)2) == 2) // Request two bytes
    {
      uint8_t msb = _i2cPort->read();
      uint8_t lsb = _i2cPort->read();
      bytesAvailable = (((uint16_t)msb) << 8) | lsb;
    }

    //Put off checking to avoid excessive I2C bus traffic - but only if zero bytes are available
    if (bytesAvailable == 0)
      _lastI2cCheck = millis();
  }

  return (bytesAvailable);
}

// Read len bytes from Qwiic Swarm, store in dest
int SWARM_M138::qwiicSwarmReadChars(int len, char *dest)
{
  if (len <= 0)
    return (len);

  if (dest == NULL)
    return (0);

  int bytesRead = 0;

  // Request the bytes
  // Release the bus afterwards
  _i2cPort->beginTransmission((uint8_t)_address); // Talk to the I2C device
  _i2cPort->write(QWIIC_SWARM_DATA_REG); // Point to the serial buffer
  _i2cPort->endTransmission(); // Send data and release the bus (the 841 (WireS) doesn't like it if the Master holds the bus!)
  while (len > QWIIC_SWARM_SER_PACKET_SIZE) // If there are _more_ than SER_PACKET_SIZE bytes to be read
  {
    _i2cPort->requestFrom((uint8_t)_address, (uint8_t)QWIIC_SWARM_SER_PACKET_SIZE, (uint8_t)false); // Request SER_PACKET_SIZE bytes, don't release the bus
    while (_i2cPort->available())
    {
      dest[bytesRead] = _i2cPort->read(); // Read and store each byte
      bytesRead++;
    }
    len -= QWIIC_SWARM_SER_PACKET_SIZE; // Decrease the number of bytes available by SER_PACKET_SIZE
  }
  _i2cPort->requestFrom((uint8_t)_address, (uint8_t)len); // Request remaining bytes, release the bus
  while (_i2cPort->available())
  {
    dest[bytesRead] = _i2cPort->read(); // Read and store each byte
    bytesRead++;
  }

  return (bytesRead);
}

// Write serial data to Qwiic Swarm
int SWARM_M138::qwiicSwarmWriteChars(int len, const char *dest)
{
  if (len <= 0)
    return (len);

  if (dest == NULL)
    return (0);

  size_t i = 0;
  size_t nexti;
  uint16_t checksum = 0;

  while (len > (QWIIC_SWARM_I2C_BUFFER_LENGTH - 3)) // If there are too many bytes to send all in one go
  {
    nexti = i + (QWIIC_SWARM_I2C_BUFFER_LENGTH - 3);
    _i2cPort->beginTransmission((uint8_t)_address);
    _i2cPort->write(QWIIC_SWARM_DATA_REG); // Point to the serial data 'register'
    for (; i < nexti; i++)
    {
        _i2cPort->write(dest[i]); // Write each byte
        checksum += (uint16_t)dest[i]; // Update the checksum
    }
    len -= (QWIIC_SWARM_I2C_BUFFER_LENGTH - 3); // Decrease the number of bytes still to send
    _i2cPort->endTransmission(); // Send data and release the bus (the 841 (WireS) doesn't like it if the Master holds the bus!)
  }

  // There are now <= (TINY_I2C_BUFFER_LENGTH - 3) bytes left to send, so send them and then release the bus
  _i2cPort->beginTransmission((uint8_t)_address);
  _i2cPort->write(QWIIC_SWARM_DATA_REG); // Point to the 'serial register'
  while (len > 0)
  {
    _i2cPort->write(dest[i]);
    checksum += (uint16_t)dest[i];
    len--;
    i++;
  }
  _i2cPort->write((uint8_t)(checksum >> 8));
  _i2cPort->write((uint8_t)(checksum & 0xFF));
  if (_i2cPort->endTransmission() != 0) //Send data and release bus
    if (_printDebug == true)
      _debugPort->println(F("qwiicSwarmWriteChars: I2C write was not successful!"));

  return ((int)i);
}

void SWARM_M138::beginSerial(unsigned long baud)
{
  if (_hardSerial != NULL)
  {
    _hardSerial->begin(baud);
  }
#ifdef SWARM_M138_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != NULL)
  {
    _softSerial->end();
    _softSerial->begin(baud);
  }
#endif
  delay(100);
}

// Allocate memory
char *SWARM_M138::swarm_m138_alloc_char(size_t num)
{
  return ((char *)new char[num]);
}
void SWARM_M138::swarm_m138_free_char(char *freeMe)
{
  delete[] freeMe;
}

//This prunes the backlog of non-actionable events. If new actionable events are added, you must modify the if statement.
void SWARM_M138::pruneBacklog()
{
  char *event;

  memset(_pruneBuffer, 0, _RxBuffSize); // Clear the _pruneBuffer

  char *preservedEvent;
  event = strtok_r(_swarmBacklog, "\n", &preservedEvent); // Look for an 'event' - something ending in \n

  while (event != NULL) //If event is actionable, add it to pruneBuffer.
  {
    // These are the events we want to keep so they can be processed by poll / checkUnsolicitedMsg
    if ((strstr(event, "$DT ") != NULL)
        || (strstr(event, "$GJ ") != NULL)
        || (strstr(event, "$GN ") != NULL)
        || (strstr(event, "$GS ") != NULL)
        || (strstr(event, "$PW ") != NULL)
        || (strstr(event, "$RD ") != NULL)
        || (strstr(event, "$RT ") != NULL)
        || (strstr(event, "$SL ") != NULL)
        || (strstr(event, "$M138 ") != NULL)
        || (strstr(event, "$TD ") != NULL))
    {
      strcat(_pruneBuffer, event); // The URCs are all readable text so using strcat is OK
      strcat(_pruneBuffer, "\n"); // strtok blows away delimiter, but we want that for later.
    }

    event = strtok_r(NULL, "\n", &preservedEvent); // Walk though any remaining events
  }

  memset(_swarmBacklog, 0, _RxBuffSize); //Clear out backlog buffer.
  memcpy(_swarmBacklog, _pruneBuffer, strlen(_pruneBuffer)); //Copy the pruned buffer back into the backlog

  swarm_m138_free_char(event);
}
