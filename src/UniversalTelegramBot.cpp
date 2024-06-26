/*
   Copyright (c) 2018 Brian Lough. All right reserved.

   UniversalTelegramBot - Library to create your own Telegram Bot using
   ESP8266 or ESP32 on Arduino IDE.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
   **** Note Regarding Client Connection Keeping ****
   Client connection is established in functions that directly involve use of
   client, i.e sendGetToTelegram, sendPostToTelegram, and
   sendMultipartFormDataToTelegram. It is closed at the end of
   sendMultipartFormDataToTelegram, but not at the end of sendGetToTelegram and
   sendPostToTelegram as these may need to keep the connection alive for respose
   / response checking. Re-establishing a connection then wastes time which is
   noticeable in user experience. Due to this, it is important that connection
   be closed manually after calling sendGetToTelegram or sendPostToTelegram by
   calling closeClient(); Failure to close connection causes memory leakage and
   SSL errors
 */

#include "UniversalTelegramBot.h"

#define ZERO_COPY(STR)    ((char*)STR.c_str())
#define BOT_CMD(STR)      buildCommand(F(STR))

UniversalTelegramBot::UniversalTelegramBot(const String& token, Client &client, int maxMessageLength) {
  updateToken(token);
  this->client = &client;
  this->maxMessageLength = maxMessageLength;
}

void UniversalTelegramBot::updateToken(const String& token) {
  _token = token;
}

String UniversalTelegramBot::getToken() {
  return _token;
}

String UniversalTelegramBot::buildCommand(const String& cmd) {
  String command;

  command += F("bot");
  command += _token;
  command += F("/");
  command += cmd;

  return command;
}

String UniversalTelegramBot::sendGetToTelegram(const String& command) {
  String body;
  
  // Connect with api.telegram.org if not already connected
  if (!client->connected()) {
    #ifdef TELEGRAM_DEBUG  
        Serial.println(F("[BOT]Connecting to server"));
    #endif
    if (!client->connect(TELEGRAM_HOST, TELEGRAM_SSL_PORT)) {
      #ifdef TELEGRAM_DEBUG  
        Serial.println(F("[BOT]Connection error"));
      #endif
    }
  }
  if (client->connected()) {

    #ifdef TELEGRAM_DEBUG  
        Serial.println("sending: " + command);
    #endif  

    client->print(F("GET /"));
    client->print(command);
    client->println(F(" HTTP/1.1"));
    client->println(F("Host:" TELEGRAM_HOST));
    client->println(F("Accept: application/json"));
    client->println(F("Cache-Control: no-cache"));
    client->println();

    readHTTPAnswer(body);
  }

  return body;
}

bool UniversalTelegramBot::readHTTPAnswer(String &body) {
  int ch_count = 0;
  unsigned long now = millis();
  bool finishedHeaders = false;
  bool currentLineIsBlank = true;
  bool responseReceived = false;
  int toRead = 0;
  String headers;

  while (millis() - now < longPoll * 1000 + waitForResponse) {
    while (client->available()) {
      char c = client->read();

      if (!finishedHeaders) {
        if (currentLineIsBlank && c == '\n') {
          finishedHeaders = true;

		  String headerLC = String(headers);
          headerLC.toLowerCase();
          int ind1 = headerLC.indexOf("content-length");
          if (ind1 != -1) {
            int ind2 = headerLC.indexOf("\r", ind1 + 15);
            if (ind2 != -1) {
              toRead = headerLC.substring(ind1 + 15, ind2).toInt();
              headers = "";
              #ifdef TELEGRAM_DEBUG
                Serial.print(F("Content-Length: "));
                Serial.println(toRead);
              #endif
            }
          }
        } else {
          headers += c;
        }
      } else {
        if (ch_count < maxMessageLength) {
          body += c;
          ch_count++;
          responseReceived = toRead > 0 ? ch_count == toRead : true;
        }
      }

      if (c == '\n') currentLineIsBlank = true;
      else if (c != '\r') currentLineIsBlank = false;
    }

    if (responseReceived) {
      break;
    }
  }

  #ifdef TELEGRAM_DEBUG
    Serial.println(F("Body:"));
    Serial.println(body);
    Serial.print(F("ch_count: "));
    Serial.println(ch_count);
  #endif

  return responseReceived;
}

String UniversalTelegramBot::sendPostToTelegram(const String& command, JsonObject payload) {

  String body;

  // Connect with api.telegram.org if not already connected
  if (!client->connected()) {
    #ifdef TELEGRAM_DEBUG  
        Serial.println(F("[BOT Client]Connecting to server"));
    #endif
    if (!client->connect(TELEGRAM_HOST, TELEGRAM_SSL_PORT)) {
      #ifdef TELEGRAM_DEBUG  
        Serial.println(F("[BOT Client]Connection error"));
      #endif
    }
  }
  if (client->connected()) {
    // POST URI
    client->print(F("POST /"));
    client->print(command);
    client->println(F(" HTTP/1.1"));
    // Host header
    client->println(F("Host:" TELEGRAM_HOST));
    // JSON content type
    client->println(F("Content-Type: application/json"));

    // Content length
    int length = measureJson(payload);
    client->print(F("Content-Length:"));
    client->println(length);
    // End of headers
    client->println();
    // POST message body
    String out;
    serializeJson(payload, out);
    
    client->println(out);
    #ifdef TELEGRAM_DEBUG
      Serial.print(F("Posting: "));
      Serial.println(out);
    #endif

    readHTTPAnswer(body);
  }

  return body;
}

String UniversalTelegramBot::sendMultipartFormDataToTelegram(
    const String& command, const String& binaryPropertyName, const String& fileName,
    const String& contentType, const String& chat_id, int fileSize,
    MoreDataAvailable moreDataAvailableCallback,
    GetNextByte getNextByteCallback, 
    GetNextBuffer getNextBufferCallback,
    GetNextBufferLen getNextBufferLenCallback) {

  String body;
  
  const String boundary = F("------------------------b8f610217e83e29b");

  // Connect with api.telegram.org if not already connected
  if (!client->connected()) {
    #ifdef TELEGRAM_DEBUG  
        Serial.println(F("[BOT Client]Connecting to server"));
    #endif
    if (!client->connect(TELEGRAM_HOST, TELEGRAM_SSL_PORT)) {
      #ifdef TELEGRAM_DEBUG  
        Serial.println(F("[BOT Client]Connection error"));
      #endif
    }
  }
  if (client->connected()) {
    String start_request;
    String end_request;
    

    start_request += F("--");
    start_request += boundary;
    start_request += F("\r\ncontent-disposition: form-data; name=\"chat_id\"\r\n\r\n");
    start_request += chat_id;
    start_request += F("\r\n" "--");
    start_request += boundary;
    start_request += F("\r\ncontent-disposition: form-data; name=\"");
    start_request += binaryPropertyName;
    start_request += F("\"; filename=\"");
    start_request += fileName;
    start_request += F("\"\r\n" "Content-Type: ");
    start_request += contentType;
    start_request += F("\r\n" "\r\n");

    end_request += F("\r\n" "--");
    end_request += boundary;
    end_request += F("--" "\r\n");

    client->print(F("POST /"));
    client->print(buildCommand(command));
    client->println(F(" HTTP/1.1"));
    // Host header
    client->println(F("Host: " TELEGRAM_HOST)); // bugfix - https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/issues/186
    client->println(F("User-Agent: arduino/1.0"));
    client->println(F("Accept: */*"));

    int contentLength = fileSize + start_request.length() + end_request.length();
    #ifdef TELEGRAM_DEBUG  
        Serial.println("Content-Length: " + String(contentLength));
    #endif
    client->print(F("Content-Length: "));
    client->println(String(contentLength));
    client->print(F("Content-Type: multipart/form-data; boundary="));
    client->println(boundary);
    client->println();
    client->print(start_request);

    #ifdef TELEGRAM_DEBUG  
     Serial.print(F("Start request: "));
     Serial.println(start_request);
    #endif

    if (getNextByteCallback == nullptr) {
        while (moreDataAvailableCallback()) {
            client->write((const uint8_t *)getNextBufferCallback(), getNextBufferLenCallback());
            #ifdef TELEGRAM_DEBUG  
             Serial.println(F("Sending photo from buffer"));
            #endif
            }
    } else {
        #ifdef TELEGRAM_DEBUG  
            Serial.println(F("Sending photo by binary"));
        #endif
        byte buffer[512];
        int count = 0;
        while (moreDataAvailableCallback()) {
            buffer[count] = getNextByteCallback();
            count++;
            if (count == 512) {
                // yield();
                #ifdef TELEGRAM_DEBUG  
                    Serial.println(F("Sending binary photo full buffer"));
                #endif
                client->write((const uint8_t *)buffer, 512);
                count = 0;
            }
        }
        
        if (count > 0) {
            #ifdef TELEGRAM_DEBUG  
                Serial.println(F("Sending binary photo remaining buffer"));
            #endif
            client->write((const uint8_t *)buffer, count);
        }
    }

    client->print(end_request);
    #ifdef TELEGRAM_DEBUG  
      Serial.print(F("End request: "));
      Serial.println(end_request);
    #endif
    readHTTPAnswer(body);
  }

  closeClient();
  return body;
}


bool UniversalTelegramBot::getMe() {
  String response = sendGetToTelegram(BOT_CMD("getMe")); // receive reply from telegram.org
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, ZERO_COPY(response));
  closeClient();

  if (!error) {
    if (doc.containsKey("result")) {
      name = doc["result"]["first_name"].as<String>();
      userName = doc["result"]["username"].as<String>();
      return true;
    }
  }

  return false;
}

/*********************************************************************************
 * SetMyCommands - Update the command list of the bot on the telegram server     *
 * (Argument to pass: Serialized array of BotCommand)                            *
 * CAUTION: All commands must be lower-case                                      *
 * Returns true, if the command list was updated successfully                    *
 ********************************************************************************/
bool UniversalTelegramBot::setMyCommands(const String& commandArray) {
  JsonDocument payload;
  payload["commands"] = serialized(commandArray);
  bool sent = false;
  String response = "";
  #ifdef TELEGRAM_DEBUG
    Serial.println(F("sendSetMyCommands: SEND Post /setMyCommands"));
  #endif
  unsigned long sttime = millis();

  while (millis() - sttime < 8000ul) { // loop for a while to send the message
    response = sendPostToTelegram(BOT_CMD("setMyCommands"), payload.as<JsonObject>());
    #ifdef TELEGRAM_DEBUG
      Serial.println(F("setMyCommands response:"));
      Serial.println(response);
    #endif
    sent = checkForOkResponse(response);
    if (sent) break;
  }

  closeClient();
  return sent;
}


/***************************************************************
 * GetUpdates - function to receive messages from telegram     *
 * (Argument to pass: the last+1 message to read)              *
 * Returns the number of new messages                          *
 ***************************************************************/
int UniversalTelegramBot::getUpdates(long offset) {

  #ifdef TELEGRAM_DEBUG  
    Serial.println(F("GET Update Messages"));
  #endif
  String command = BOT_CMD("getUpdates?offset=");
  command += offset;
  command += F("&limit=");
  command += HANDLE_MESSAGES;

  if (longPoll > 0) {
    command += F("&timeout=");
    command += String(longPoll);
  }
  String response = sendGetToTelegram(command); // receive reply from telegram.org
  long updateId = getUpdateIdFromResponse(response);

  if (response == "") {
    #ifdef TELEGRAM_DEBUG  
        Serial.println(F("Received empty string in response!"));
    #endif
    // close the client as there's nothing to do with an empty string
    closeClient();
    return 0;
  } else {
    #ifdef TELEGRAM_DEBUG  
      Serial.print(F("incoming message length "));
      Serial.println(response.length());
      Serial.println(F("Creating DynamicJsonBuffer"));
    #endif

    // Parse response into Json object
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, ZERO_COPY(response));
      
    if (!error) {
      #ifdef TELEGRAM_DEBUG  
        Serial.print(F("GetUpdates parsed jsonObj: "));
        serializeJson(doc, Serial);
        Serial.println();
      #endif
      if (doc.containsKey("result")) {
        int resultArrayLength = doc["result"].size();
        if (resultArrayLength > 0) {
          int newMessageIndex = 0;
          // Step through all results
          for (int i = 0; i < resultArrayLength; i++) {
            JsonObject result = doc["result"][i];
            if (processResult(result, newMessageIndex)) newMessageIndex++;
          }
          // We will keep the client open because there may be a response to be
          // given
          return newMessageIndex;
        } else {
          #ifdef TELEGRAM_DEBUG  
            Serial.println(F("no new messages"));
          #endif
        }
      } else {
        #ifdef TELEGRAM_DEBUG  
            Serial.println(F("Response contained no 'result'"));
        #endif
      }
    } else { // Parsing failed
      Serial.print(F("Update ID with error: "));
      Serial.println(updateId);

      if (response.length() < 2) { // Too short a message. Maybe a connection issue
        #ifdef TELEGRAM_DEBUG  
            Serial.println(F("Parsing error: Message too short"));
        #endif
      } else {
        // Buffer may not be big enough, increase buffer or reduce max number of
        // messages
        #ifdef TELEGRAM_DEBUG 
            Serial.print(F("Failed to parse update, the message could be too "
                           "big for the buffer. Error code: "));
            Serial.println(error.c_str()); // debug print of parsing error
        #endif     
      }
    }
    // Close the client as no response is to be given
    closeClient();

    if (error && response.length() == (unsigned) maxMessageLength) {
        Serial.print(F("The message with update ID "));
        Serial.print(updateId);
        Serial.println(F(" is too long and was skipped. The next update ID has been sent for processing."));

        return getUpdates(updateId + 1);
    }

    return 0;
  }
}

bool UniversalTelegramBot::processResult(JsonObject result, int messageIndex) {
  long update_id = result["update_id"];
  // Check have we already dealt with this message (this shouldn't happen!)
  if (last_message_received != update_id) {
    last_message_received = update_id;
    messages[messageIndex].update_id = update_id;
    messages[messageIndex].text = F("");
    messages[messageIndex].from_id = F("");
    messages[messageIndex].from_name = F("");
    messages[messageIndex].longitude = 0;
    messages[messageIndex].latitude = 0;
    messages[messageIndex].reply_to_message_id = 0;
    messages[messageIndex].reply_to_text = F("");
    messages[messageIndex].query_id = F("");
    messages[messageIndex].contact_phone_number = F("");
    messages[messageIndex].contact_name = F("");
    messages[messageIndex].contact_id = F("");    

    if (result.containsKey("message")) {
      JsonObject message = result["message"];
      messages[messageIndex].type = F("message");
      messages[messageIndex].from_id = message["from"]["id"].as<String>();
      messages[messageIndex].from_name = message["from"]["first_name"].as<String>();
      messages[messageIndex].date = message["date"].as<String>();
      messages[messageIndex].chat_id = message["chat"]["id"].as<String>();
      messages[messageIndex].chat_title = message["chat"]["title"].as<String>();
      messages[messageIndex].hasDocument = false;
      messages[messageIndex].message_id = message["message_id"].as<int>();  // added message id
      if (message.containsKey("text")) {
        messages[messageIndex].text = message["text"].as<String>();
          
      } else if (message.containsKey("location")) {
        messages[messageIndex].longitude = message["location"]["longitude"].as<float>();
        messages[messageIndex].latitude  = message["location"]["latitude"].as<float>();
      } else if (message.containsKey("document")) {
        String file_id = message["document"]["file_id"].as<String>();
        messages[messageIndex].file_caption = message["caption"].as<String>();
        messages[messageIndex].file_name = message["document"]["file_name"].as<String>();
        if (getFile(messages[messageIndex].file_path, messages[messageIndex].file_size, file_id) == true)
          messages[messageIndex].hasDocument = true;
        else
          messages[messageIndex].hasDocument = false;
      } else if (message.containsKey("contact")) {
          messages[messageIndex].contact_phone_number = message["contact"]["phone_number"].as<String>();
          messages[messageIndex].contact_name = message["contact"]["first_name"].as<String>();
          messages[messageIndex].contact_id = message["contact"]["user_id"].as<String>();
      }
      if (message.containsKey("reply_to_message")) {
        messages[messageIndex].reply_to_message_id = message["reply_to_message"]["message_id"];
        // no need to check if containsKey["text"]. If it doesn't, it default to null
        messages[messageIndex].reply_to_text = message["reply_to_message"]["text"].as<String>();
      }

    } else if (result.containsKey("channel_post")) {
      JsonObject message = result["channel_post"];
      messages[messageIndex].type = F("channel_post");
      messages[messageIndex].text = message["text"].as<String>();
      messages[messageIndex].date = message["date"].as<String>();
      messages[messageIndex].chat_id = message["chat"]["id"].as<String>();
      messages[messageIndex].chat_title = message["chat"]["title"].as<String>();
      messages[messageIndex].message_id = message["message_id"].as<int>();  // added message id

    } else if (result.containsKey("callback_query")) {
      JsonObject message = result["callback_query"];
      messages[messageIndex].type = F("callback_query");
      messages[messageIndex].from_id = message["from"]["id"].as<String>();
      messages[messageIndex].from_name = message["from"]["first_name"].as<String>();
      messages[messageIndex].text = message["data"].as<String>();
      messages[messageIndex].date = message["date"].as<String>();
      messages[messageIndex].chat_id = message["message"]["chat"]["id"].as<String>();
      messages[messageIndex].reply_to_text = message["message"]["text"].as<String>();
      messages[messageIndex].chat_title = F("");
      messages[messageIndex].query_id = message["id"].as<String>();
      messages[messageIndex].message_id = message["message"]["message_id"].as<int>();  // added message id

    } else if (result.containsKey("edited_message")) {
      JsonObject message = result["edited_message"];
      messages[messageIndex].type = F("edited_message");
      messages[messageIndex].from_id = message["from"]["id"].as<String>();
      messages[messageIndex].from_name = message["from"]["first_name"].as<String>();
      messages[messageIndex].date = message["date"].as<String>();
      messages[messageIndex].chat_id = message["chat"]["id"].as<String>();
      messages[messageIndex].chat_title = message["chat"]["title"].as<String>();
      messages[messageIndex].message_id = message["message_id"].as<int>();  // added message id

      if (message.containsKey("text")) {
        messages[messageIndex].text = message["text"].as<String>();
          
      } else if (message.containsKey("location")) {
        messages[messageIndex].longitude = message["location"]["longitude"].as<float>();
        messages[messageIndex].latitude  = message["location"]["latitude"].as<float>();
      }
    }
    return true;
  }
  return false;
}

/***********************************************************************
 * SendMessage - function to send message to telegram                  *
 * (Arguments to pass: chat_id, text to transmit and markup(optional)) *
 ***********************************************************************/
bool UniversalTelegramBot::sendSimpleMessage(const String& chat_id, const String& text,
                                             const String& parse_mode) {

  bool sent = false;
  #ifdef TELEGRAM_DEBUG  
    Serial.println(F("sendSimpleMessage: SEND Simple Message"));
  #endif
  unsigned long sttime = millis();

  if (text != "") {
    while (millis() - sttime < 8000ul) { // loop for a while to send the message
      String command = BOT_CMD("sendMessage?chat_id=");
      command += chat_id;
      command += F("&text=");
      command += text;
      command += F("&parse_mode=");
      command += parse_mode;
      String response = sendGetToTelegram(command);
      #ifdef TELEGRAM_DEBUG  
        Serial.println(response);
      #endif
      sent = checkForOkResponse(response);
      if (sent) break;
    }
  }
  closeClient();
  return sent;
}

bool UniversalTelegramBot::sendMessage(const String& chat_id, const String& text,
                                       const String& parse_mode, int message_id, bool disable_web_page_preview,
                                       bool disable_notification) {

  JsonDocument payload;
  payload["chat_id"] = chat_id;
  payload["text"] = text;

  if (message_id != 0)
    payload["message_id"] = message_id; // added message_id

  if (parse_mode != "")
    payload["parse_mode"] = parse_mode;

  if (disable_web_page_preview)
    payload["disable_web_page_preview"] = disable_web_page_preview;

  if (disable_notification)
    payload["disable_notification"] = disable_notification;

  return sendPostMessage(payload.as<JsonObject>(), message_id); // if message id == 0 then edit is false, else edit is true
}

/***********************************************************************
 * DeleteMessage - function to delete message by message_id            *
 * Function description and limitations:                               *
 * https://core.telegram.org/bots/api#deletemessage                    *
 ***********************************************************************/
bool UniversalTelegramBot::deleteMessage(const String& chat_id, int message_id) {
  if (message_id == 0)
  {
    #ifdef TELEGRAM_DEBUG
	  Serial.println(F("deleteMessage: message_id not passed for deletion"));
	#endif
    return false;
  }

  JsonDocument payload;
  payload["chat_id"] = chat_id;
  payload["message_id"] = message_id;

  #ifdef TELEGRAM_DEBUG
    Serial.print(F("deleteMessage: SEND Post Message: "));
    serializeJson(payload, Serial);
    Serial.println();
  #endif

  String response = sendPostToTelegram(BOT_CMD("deleteMessage"), payload.as<JsonObject>());
  #ifdef TELEGRAM_DEBUG
     Serial.print(F("deleteMessage response:"));
     Serial.println(response);
  #endif

  bool sent = checkForOkResponse(response);
  closeClient();
  return sent;
}

bool UniversalTelegramBot::sendMessageWithReplyKeyboard(
    const String& chat_id, const String& text, const String& parse_mode, const String& keyboard,
    bool resize, bool oneTime, bool selective) {
    
  JsonDocument payload;
  payload["chat_id"] = chat_id;
  payload["text"] = text;

  if (parse_mode != "")
    payload["parse_mode"] = parse_mode;

  JsonObject replyMarkup = payload["reply_markup"].to<JsonObject>();
  
  if (keyboard.isEmpty())
    replyMarkup["remove_keyboard"] = true;
  else
    replyMarkup["keyboard"] = serialized(keyboard);

  // Telegram defaults these values to false, so to decrease the size of the
  // payload we will only send them if needed
  if (resize)
    replyMarkup["resize_keyboard"] = resize;

  if (oneTime)
    replyMarkup["one_time_keyboard"] = oneTime;

  if (selective)
    replyMarkup["selective"] = selective;

  return sendPostMessage(payload.as<JsonObject>());
}

bool UniversalTelegramBot::sendMessageWithInlineKeyboard(const String& chat_id,
                                                         const String& text,
                                                         const String& parse_mode,
                                                         const String& keyboard,
                                                         int message_id) {

  JsonDocument payload;
  payload["chat_id"] = chat_id;
  payload["text"] = text;

  if (message_id != 0)
    payload["message_id"] = message_id; // added message_id
    
  if (parse_mode != "")
    payload["parse_mode"] = parse_mode;

  JsonObject replyMarkup = payload["reply_markup"].to<JsonObject>();
  replyMarkup["inline_keyboard"] = serialized(keyboard);
  return sendPostMessage(payload.as<JsonObject>(), message_id); // if message id == 0 then edit is false, else edit is true
}

/***********************************************************************
 * SendPostMessage - function to send message to telegram              *
 * (Arguments to pass: chat_id, text to transmit and markup(optional)) *
 ***********************************************************************/
bool UniversalTelegramBot::sendPostMessage(JsonObject payload, bool edit) { // added message_id

  bool sent = false;
  #ifdef TELEGRAM_DEBUG 
    Serial.print(F("sendPostMessage: SEND Post Message: "));
    serializeJson(payload, Serial);
    Serial.println();
  #endif 
  unsigned long sttime = millis();

  if (payload.containsKey("text")) {
    while (millis() < sttime + 8000) { // loop for a while to send the message
        String response = sendPostToTelegram((edit ? BOT_CMD("editMessageText") : BOT_CMD("sendMessage")), payload); // if edit is true we send a editMessageText CMD
         #ifdef TELEGRAM_DEBUG  
        Serial.println(response);
      #endif
      sent = checkForOkResponse(response);
      if (sent) break;
    }
  }

  closeClient();
  return sent;
}

String UniversalTelegramBot::sendPostPhoto(JsonObject payload) {

  bool sent = false;
  String response = "";
  #ifdef TELEGRAM_DEBUG  
    Serial.println(F("sendPostPhoto: SEND Post Photo"));
  #endif
  unsigned long sttime = millis();

  if (payload.containsKey("photo")) {
    while (millis() - sttime < 8000ul) { // loop for a while to send the message
      response = sendPostToTelegram(BOT_CMD("sendPhoto"), payload);
      #ifdef TELEGRAM_DEBUG  
        Serial.println(response);
      #endif
      sent = checkForOkResponse(response);
      if (sent) break;
      
    }
  }

  closeClient();
  return response;
}

String UniversalTelegramBot::sendPhotoByBinary(
    const String& chat_id, const String& contentType, int fileSize,
    MoreDataAvailable moreDataAvailableCallback,
    GetNextByte getNextByteCallback, GetNextBuffer getNextBufferCallback, GetNextBufferLen getNextBufferLenCallback) {

  #ifdef TELEGRAM_DEBUG  
    Serial.println(F("sendPhotoByBinary: SEND Photo"));
  #endif

  String response = sendMultipartFormDataToTelegram("sendPhoto", "photo", "img.jpg",
    contentType, chat_id, fileSize,
    moreDataAvailableCallback, getNextByteCallback, getNextBufferCallback, getNextBufferLenCallback);

  #ifdef TELEGRAM_DEBUG  
    Serial.println(response);
  #endif

  return response;
}

String UniversalTelegramBot::sendPhoto(const String& chat_id, const String& photo,
                                       const String& caption,
                                       bool disable_notification,
                                       int reply_to_message_id,
                                       const String& keyboard) {

  JsonDocument payload;
  payload["chat_id"] = chat_id;
  payload["photo"] = photo;

  if (caption.length() > 0)
      payload["caption"] = caption;

  if (disable_notification)
      payload["disable_notification"] = disable_notification;

  if (reply_to_message_id && reply_to_message_id != 0)
      payload["reply_to_message_id"] = reply_to_message_id;

  if (keyboard.length() > 0) {
    JsonObject replyMarkup = payload["reply_markup"].to<JsonObject>();
    replyMarkup["keyboard"] = serialized(keyboard);
  }

  return sendPostPhoto(payload.as<JsonObject>());
}

bool UniversalTelegramBot::checkForOkResponse(const String& response) {
  int last_id;
  JsonDocument doc;
  deserializeJson(doc, response);

  // Save last sent message_id
  last_id = doc["result"]["message_id"];
  if (last_id > 0) last_sent_message_id = last_id;

  return doc["ok"] | false;  // default is false, but this is more explicit and clear
}

bool UniversalTelegramBot::sendChatAction(const String& chat_id, const String& text) {

  bool sent = false;
  #ifdef TELEGRAM_DEBUG  
    Serial.println(F("SEND Chat Action Message"));
  #endif
  unsigned long sttime = millis();

  if (text != "") {
    while (millis() - sttime < 8000ul) { // loop for a while to send the message
      String command = BOT_CMD("sendChatAction?chat_id=");
      command += chat_id;
      command += F("&action=");
      command += text;

      String response = sendGetToTelegram(command);

      #ifdef TELEGRAM_DEBUG  
        Serial.println(response);
      #endif
      sent = checkForOkResponse(response);

      if (sent) break;
      
    }
  }

  closeClient();
  return sent;
}

void UniversalTelegramBot::closeClient() {
  if (client->connected()) {
    #ifdef TELEGRAM_DEBUG  
        Serial.println(F("Closing client"));
    #endif
    client->stop();
  }
}

bool UniversalTelegramBot::getFile(String& file_path, long& file_size, const String& file_id)
{
  String command = BOT_CMD("getFile?file_id=");
  command += file_id;
  String response = sendGetToTelegram(command); // receive reply from telegram.org
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, ZERO_COPY(response));
  closeClient();

  if (!error) {
    if (doc.containsKey("result")) {
      file_path  = F("https://api.telegram.org/file/");
      file_path += buildCommand(doc["result"]["file_path"]);
      file_size = doc["result"]["file_size"].as<long>();
      return true;
    }
  }
  return false;
}

bool UniversalTelegramBot::answerCallbackQuery(const String &query_id, const String &text, bool show_alert, const String &url, int cache_time) {
  JsonDocument payload;

  payload["callback_query_id"] = query_id;
  payload["show_alert"] = show_alert;
  payload["cache_time"] = cache_time;

  if (text.length() > 0) payload["text"] = text;
  if (url.length() > 0) payload["url"] = url;

  String response = sendPostToTelegram(BOT_CMD("answerCallbackQuery"), payload.as<JsonObject>());
  #ifdef TELEGRAM_DEBUG
     Serial.print(F("answerCallbackQuery response:"));
     Serial.println(response);
  #endif
  bool answer = checkForOkResponse(response);
  closeClient();
  return answer;
}

long UniversalTelegramBot::getUpdateIdFromResponse(String response) {
  response.remove(response.indexOf("\n"));

  char updateId[20];
  const char *str = response.c_str();

  while(*str != '\0')
  {
    if (*str == '\r') {
      break;
    }

    str++;

    int i = 0;
    while('0' <= *str && *str <= '9')
    {
      updateId[i] = *str;
      i++;
      str++;
    }
  }

  return atol(updateId);
}