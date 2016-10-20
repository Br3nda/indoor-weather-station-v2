#ifndef CAPTIVE_CONFIG_HEADER
#define CAPTIVE_CONFIG_HEADER

#include <IPAddress.h>

#include "HouseSensor.h"

class ESP8266WebServer;
class DNSServer;

/// Name of the access point we present.
#define APNAME "VCW Indoor Weather Station"

/// IP address of the ESP8266 when we're serving the captive config page
const static IPAddress captiveServeIP(192, 168, 1, 1);

/// Store information on WiFi networks between scanning and serving
struct APType
{
    String ssid;
    int32_t rssi; // Signal strength
    uint8_t encryptionType; // = ENC_TYPE_NONE for open networks
};

/// Used to provide a "Captive Portal" so the user can set WiFi Passwords, etc.
/*!
 * Should only ever be one of these created at a time; ESP8266WebServer uses
 * callbacks to handle requests, and can only deal with one client at a time.
 *
 * Usage:
 *
 *   CaptiveConfig *configGetter(new CaptiveConfig);
 *
 *   while(!configGetter->haveConfig())
 *       // DNS and HTTP servers start up, user makes config selections.
 *       do_other_stuff();
 * 
 *   // Retrieve the AP credentials
 *   auto receivedConfig(configGetter->getCredentials());
 *
 *   // Retrieve registration email
 *   auto receivedConfig(configGetter->getEmail());
 *
 *   // done
 *   delete configGetter;
 * */
class CaptiveConfig
{
    public:

        CaptiveConfig();
        ~CaptiveConfig();
        
        /// Advances state machine, handles requests. Returns true when done.
        bool haveConfig();

        /// Returns the SSID + Passphase selected
        APCredentials getCredentials() const;

        /// Returns the registration email address
        String getEmail() const;
        
    protected:

        enum class CaptiveConfigState {
            START_SCANNING,
            SCANNING,
            STARTING_WIFI,
            STARTING_HTTP,
            STARTING_DNS,
            SERVING,
            DONE,
        } state;

        ESP8266WebServer *configHTTPServer;

        DNSServer *configDNSServer;

        /// Number of networks in knownAPs
        int numAPsFound;

        /// Array of pointers to known access points.
        /*!
         * At maximum, numAPsFound of these will be valid.  Others will be
         * set to nullptr and at the high end of the array.
         */
        APType **knownAPs;

        /// Fill knownAPs with a set of different networks we can see.
        /*!
         * Resolve duplicates by keeping the strongest signals for a given
         * SSID.  numAPs is the number of APs reported by scanNetworks.
         */
        void populateKnownAPs(uint8_t numAPs);

        /// Tears down the structure of known APs
        void tearDownKnownAPs();

        /// Returns JSON object with the SSIDs known to the weather station
        String makeApJson() const;

        /// Serves a JSON object from makeApJson()
        static void serveApJson();

        /// Handles the password entered in to the configuration page
        static void storePassword();
        
        /// Serves up the main configuration page
        static void serveConfigPage();

        /// Sends a 302 redirect to "http://setup/" (the main config page)
        static void serveRedirect();

        /// For callbacks, static functions, etc.
        static CaptiveConfig *instance;

        APCredentials *pickedCreds;

        String registrationEmail;
}; // end class CaptiveConfig

#endif // #ifndef CAPTIVE_CONFIG_HEADER

