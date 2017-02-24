## Architecture

OAP is built on the top of AWS Iot.

Each sensor is identified by a unique name.

Senors can connect with OAP via REST or MQTT.
Connections are secured and authentication is performed based on certificate issued by OAP when new sensor gets registered.

Once connected, sensor can update its **state** (represented as a JSON tree) in the OAP cloud by sending json **state-reported** message.
It can also receive **state-desired** requests from OAP, this feature however is fully optional and can be used to remotely change current configuration of connected sensors.

## Sensor API

State itself has no particular schema and no validation is performed - it can be any value you want to persist in OAP cloud.
However, to create a record in OAP result tables (and therefore - publish the results), state-reported message has to meet some conditions and contain at least 'result.uid' field. 

	{
	  "state" : {
	    "reported" : {
	      /**
	       * All parameters below ARE OPTIONAL.
	       * Thing shadow will get updated with any values posted here (there's no validation),
	       * however system will record a measurement only when some specifics conditions are met. See below.
	       */
	
	      /**
	       * local time of measurement in epoch seconds.
	       * if this value is present, it is different than previously recorded and it falls into valid range, server will accept
	       * this as a time of measurement. Otherwise server time will be used.
	       * This field enables you to report historical measurements (for example taken when sensor is offline) - but only
	       * if your sensor is equipped with Real-Time Clock.
	       */
	      "localTime" : 0,
	
	      "results" : {
	        /**
	         * this parameter is required for server to accept incoming data as a measurement.
	         * if this value is missing (or null, undefined, 0 or empty string) or equal to previously sent uid,
	         * thing shadow will get updated but result tables will not.
	         */
	        "uid" : 1,
	        /**
	         * pollution results. all parameters are optional but when present, types must match.
	         * if particular parameter is missing, but was reported previously - the old value will be taken.
	         */
	        "pm" : {
	          "pm1_0"   : 0,
	          "pm2_5"   : 0,
	          "pm10"    : 0,
	
	          /*
	           * this parameter determines sensor used for measurement. used by meters with multiple PM sensors.
	           */
	          "sensor"  : 0
	        },
	        /**
	         * measured atmospheric conditions. parameters are optional but types are validated.
	         */
	        "weather" : {
	          "temp" : 0.0,
	          "pressure" : 0.0,
	          "humidity" : 0.0,
	          "sensor" : 0
	        }
	      },
	      /**
	       * current configuration of the sensor.
	       */
	      "config" : {
	        /**
	         * sent it when sensor changed its location from outdoor to indoor or vice-versa.
	         */
	        "indoor" : false,
	        /**
	         * any value other than 0 means that sensor is running in test mode and results should be ignored from main metrics.
	         */
	        "test"  : 0,
	        /**
	          * optional location data for mobile sensors
	          */
	        "location" : {
	          "lat" : 0.0,
	          "lng" : 0.0
	        }
	      }
	    }
	  }
	}
	
## Data API

Results are currently stored in three DynamoDB tables:

	1. OAP_ALL  (all recorded measurements)
	2. OAP_HOUR (one measurement per hour)
	3. OAP_LAST (last recorded measurement)
	
All registered sensors are listed in OAP_THINGS table.
Anonymous users have read-only access to these tables and can use them via DynamoDB AWS-SDK api.

