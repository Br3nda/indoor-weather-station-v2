#ifndef DATA_UPLOADER_HEADER
#define DATA_UPLOADER_HEADER

#include "HouseSensor.h"

#include <ESP8266WiFi.h>

#define DATAUPLOADER_USE_HTTPS false
#define DATAUPLOADER_SERVER_URL "someurl.org.nz"
#define DATAUPLOADER_SERVER_PORT 80

/// Connects to the WiFi AP, uploads data to server, etc.
/*!
 * The DataUploader has a static list of "known" access points, which it will
 * attempt to use if there is a problem using the user-specified access point
 * (or if none is provided).
 */
class DataUploader
{
    public:
        /// preferredAP is the set of credentials input by the user
        DataUploader( char *uploadData, size_t uploadLen,
                      APCredentials *preferredAP = nullptr );

        /// Puts the WiFi in to sleep mode
        ~DataUploader();

        /// Advances state machine, returns true when we're done
        bool isDone();

        /// If isDone() returns true, then this returns true iff we succeeded.
        bool succeeded() const;

    protected:
        /// Advance to next AP from staticAPs, or set state to failure.
        void tryNextAp();

        /// Called after we've connected to WiFi, returns true if successful.
        bool doUpload();

        static void wifiConnectCb(const WiFiEventStationModeConnected &);
        static void wifiDisconnectCb(const WiFiEventStationModeDisconnected &);
        static void wifiAuthChangedCb(const WiFiEventStationModeAuthModeChanged &);
        static void wifiGotIpCb(const WiFiEventStationModeGotIP &);
        static void wifiDhcpTimeoutCb(void);

        enum class DataUploaderState {
            TRYING_ACCESS_POINT,
            WIFI_TBD, // TODO: Hack - remove
            SUCCESS,
            CANT_CONNECT_TO_ANY,
        } state;

        /// -1 if have requested AP, otherwise indexes in to staticAPs
        int nextAPIndex;

        /// Used to time out connection attempts
        int connectCountdown;

        /// Set to the user's preferred AP, or empty string if using the list.
        String requestedSSID, requestedPassphrase;

        /// For callbacks, static functions, etc.
        static DataUploader *instance;

        /// Data we're uploading - owned by caller
        char *uploadDataPtr;

        /// Length in bytes of data to upload
        size_t uploadDataLen;
}; // end class DataUploader

#endif // #ifndef DATA_UPLOADER_HEADER

