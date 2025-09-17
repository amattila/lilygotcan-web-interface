/*
 * This file is part of the esp8266 web interface
 *
 * Copyright (C) 2018 Johannes Huebner <dev@johanneshuebner.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


/** @brief this is a little cache to store the current params/spot values. This
 * is here so that different functions can do look-ups without making a full
 * HTTP call to the inverter each time. */

var paramsCache = {
  data: undefined,
  dataById: {},
  failedFetchCount: 0,

  get: function(name) {
    if ( paramsCache.data !== undefined )
    {
      if ( name in paramsCache.data ) {
        if ( paramsCache.data[name].enums ) {
              return paramsCache.data[name].enums[paramsCache.data[name].value];
          } else {
            return paramsCache.data[name].value;
          }
      }
    }
    return null;
  },

  getEntry: function(name) {
    return paramsCache.data[name];
  },

  getData: function() { return paramsCache.data; },

  setData: function(data) {
    paramsCache.data = data;

    for (var key in data) {
      if (data[key].id !== undefined) {
        paramsCache.dataById[data[key].id] = data[key];
        paramsCache.dataById[data[key].id].name = key;
      }
    }
  },

  getJson: function() { return JSON.stringify(paramsCache.data); },

  getById: function(id) {
    return paramsCache.dataById[id];
  }
}

var inverter = {

  firmwareVersion: 0,

  /** @brief send a command to the inverter */
  sendCmd: function(cmd, replyFunc, repeat)
  {
    var xmlhttp=new XMLHttpRequest();
    var req = "/cmd?cmd=" + cmd;

    xmlhttp.onload = function() {
      console.log(req + ": " + xmlhttp.status);
      if (xmlhttp.status != 200) {
        paramsCache.failedFetchCount += 1;
        if ( paramsCache.failedFetchCount >= 2 ){
          ui.showCommunicationErrorBar();
        }
      }
      else {
        paramsCache.failedFetchCount = 0;
        if (replyFunc) replyFunc(this.responseText);
      }
      if ( paramsCache.failedFetchCount < 2 ) {
        ui.hideCommunicationErrorBar();
      }
    }

    xmlhttp.onerror = function() {
      console.log(req + ": Network error");
      paramsCache.failedFetchCount += 1;
      if ( paramsCache.failedFetchCount >= 2 ){
        ui.showCommunicationErrorBar();
      }
    }

    if (repeat)
      req += "&repeat=" + repeat;

    xmlhttp.open("GET", req, true);
    xmlhttp.send();
  },

  /** @brief get the params from the inverter */
  getParamList: function(replyFunc, includeHidden)
  {
    var cmd = includeHidden ? "json hidden" : "json";

    inverter.sendCmd(cmd, function(reply) {
      var params = {};
      try {
        params = JSON.parse(reply);

        for (var name in params)
        {
          var param = params[name];
          param.enums = inverter.parseEnum(param.unit);

          if (name == "version")
            inverter.firmwareVersion = parseFloat(param.value);
        }
      } catch(ex) {
        console.error("Failed to parse parameter list JSON:", ex, "Reply:", reply);
      }

      paramsCache.setData(params);
      if (replyFunc) replyFunc(params);
    }, null, function(error) {
      console.error("Failed to get parameter list:", error);
      paramsCache.setData({});
      if (replyFunc) replyFunc({});
    });
  },

  /** @brief get CAN mapping from the inverter - only meant for wifi <-> can bridge */
  canMapping: function(replyFunc, args = "") {
    var xmlhttp = new XMLHttpRequest();
    var req = "/canmap" + args;

    xmlhttp.onload = function()
    {
      if (xmlhttp.status === 200 && replyFunc) {
        try {
          replyFunc(JSON.parse(this.responseText));
        } catch(ex) {
          console.error("Failed to parse CAN mapping JSON:", ex, "Reply:", this.responseText);
          replyFunc([]);
        }
      } else if (replyFunc) {
        console.warn("CAN mapping request failed with status:", xmlhttp.status);
        // Call replyFunc with empty array to avoid breaking UI
        replyFunc([]);
      }
    }

    xmlhttp.onerror = function() {
      console.error("CAN mapping request failed:", req);
      if (replyFunc) replyFunc([]);
    }

    xmlhttp.open("GET", req, true);
    xmlhttp.send();
  },

  /** @brief Delete a CAN mapping
   *
   * @param replyFunc function called with updated mapping info after deletion
   * @param index index of message to be deleted
   * @param subindex index of item within message to be deleted
   *
   */
  canDelete: function(replyFunc, index, subindex) {
    inverter.canMapping(replyFunc, "?remove={\"index\":" + index + ",\"subindex\":" + subindex + "}");
  },

  /** @brief Add a CAN mapping
   * @param direction true for rx, false for tx
   * @param name, spot value name
   * @param id, canid of message
   * @param pos, offset within frame
   * @param bits, length of field
   * @param gain, multiplier
   */
  addCanMapping: function(replyFunc, map) {
    inverter.canMapping(replyFunc, "?add=" + JSON.stringify(map));
  },

  getValues: function(items, repeat, replyFunc)	{
    var process = function(reply)	{
      var expr = /(\-{0,1}[0-9]+\.[0-9]*)/mg;
      var signalIdx = 0;
      var values = {};

      for (var res = expr.exec(reply); res; res = expr.exec(reply))
      {
        var val = parseFloat(res[1]);

        if (!values[items[signalIdx]])
          values[items[signalIdx]] = new Array()
        values[items[signalIdx]].push(val);
        signalIdx = (signalIdx + 1) % items.length;
      }
      replyFunc(values);
    };

    if (inverter.firmwareVersion < 3.53 || items.length > 10)
      inverter.sendCmd("get " + items.join(','), process, repeat);
    else
      inverter.sendCmd("stream " + repeat + " " + items.join(','), process);
  },


  /** @brief given the 'unit' string provided by the inverter api, parse out
   * the key value pairs and return them in an array.
   * @param unit, e.g. "0=None, 1=UdcLow, 2=UdcHigh, 4=UdcBelowUdcSw"
   * Example return : ['None', 'UdcLow', 'UdcHigh',,'udcBelowUdcSw']. Note,
   * the extra comma is intentional. The position in the array is determined
   * by the index on the left hand side of the equals in the 'unit' string.
   */
  parseEnum: function(unit)
  {
    if (!unit || typeof unit !== 'string') {
      return false;
    }

    var expr = /(\-{0,1}[0-9]+)=([a-zA-Z0-9_\-\.]+)/g;
    var enums = [];
    var res;

    while ((res = expr.exec(unit)) !== null)
    {
      var index = parseInt(res[1], 10);
      var value = res[2];
      enums[index] = value;
    }

    return enums.length > 0 ? enums : false;
  },

  /** @brief helper function, from a list of parameters send parameter with given index to inverter
   * @param params map of parameters (name -> value)
   * @param index numerical index which parameter to set */
  setParam: function(params, startIndex)
  {
    var keys = Object.keys(params);
    var index = startIndex || 0;

    function setNextParam() {
      if (index < keys.length) {
        var key = keys[index];
        modal.appendToModal('large', "Setting " + key + " to " + params[key] + "<br>");
        inverter.sendCmd("set " + key + " " + params[key], function(reply) {
          modal.appendToModal('large', reply + "<br>");
          // auto-scroll text in modal as it is added
          modal.largeModalScrollToBottom();
          index++;
          setNextParam();
        });
      }
    }

    setNextParam();
  },



    /** @brief get a list of files in the spiffs filesystem on the esp8266 */
  getFiles: function(replyFunc)
  {
    var filesRequest = new XMLHttpRequest();
    filesRequest.onload = function()
    {
      try {
        var filesJson = JSON.parse(this.responseText);
        replyFunc(filesJson);
      } catch(ex) {
        console.error("Failed to parse files JSON:", ex);
      }
    }
    filesRequest.onerror = function()
    {
      alert("error");
    }
    filesRequest.open("GET", "/list", true);
    filesRequest.send();
  },

     /** @brief get a list of files in the SD card filesystem */
  getSDFiles: function(replyFunc)
     {
       var filesRequest = new XMLHttpRequest();
       filesRequest.onload = function()
       {
         if (this.responseText == 'FileNotFound') {
          alert('Cannot open SD card')
         } else {
          try {
            var filesJson = JSON.parse(this.responseText);
            replyFunc(filesJson);
          } catch(ex) {
            console.error("Failed to parse SD files JSON:", ex);
          }
         }
 
       }
       filesRequest.onerror = function()
       {
         alert("error");
       }
       filesRequest.open("GET", "/sdcard/list", true);
       filesRequest.send();
     },

  /** @brief delete a file from the spiffs filessytem on the esp8266 */
  deleteFile: function(filename, replyFunc)
  {
    var deleteFileRequest = new XMLHttpRequest();
    deleteFileRequest.onload = function()
    {
      try {
        var responseJson = JSON.parse(this.responseText);
        replyFunc(responseJson);
      } catch(ex) {
        console.error("Failed to parse delete file response JSON:", ex);
      }
    }
    deleteFileRequest.onerror = function()
    {
      alert("error");
    }
    deleteFileRequest.open("DELETE", "/edit?f=" + filename, true);
    deleteFileRequest.send();
  }


};
