#include "DataUploader.h"

#include <cassert>

/// Used for the static list of public APs that we know about at build time
/*!
 * The loginUrl is for public access points that require clicking an "I agree"
 * button to get access.
 */
struct StaticAPInfo
{
    char *ssid;
    char *passphrase;
    char *loginUrl;
};

#pragma GCC diagnostic push
// warning: deprecated conversion from string constant to 'char*'
#pragma GCC diagnostic ignored "-Wwrite-strings"
StaticAPInfo staticAPs[]{ {"Wicked",  "", ""},
                          {"Library", "", ""} };
#pragma GCC diagnostic pop

// Units determined by length of delay in main loop()
#define DATAUPLOADER_WIFI_CONNECT_TIMEOUT 5000

DataUploader * DataUploader::instance(nullptr);

DataUploader::DataUploader( char *uploadData, size_t uploadLen,
                            APCredentials *preferredAP /* = nullptr */ ) :
    uploadDataPtr(uploadData),
    uploadDataLen(uploadLen)
{
    assert(instance == nullptr);
    instance = this;

    if( preferredAP ) {
        nextAPIndex = -1;
        requestedSSID = preferredAP->ssid;
        requestedPassphrase = preferredAP->passphrase;
    } else {
        nextAPIndex = 0;
    }

    WiFi.forceSleepWake();
    WiFi.enableSTA(true);

    // These don't seem to be setup properly...
    WiFi.onStationModeConnected(wifiConnectCb);
    WiFi.onStationModeDisconnected(wifiDisconnectCb);
    WiFi.onStationModeAuthModeChanged(wifiAuthChangedCb);
    WiFi.onStationModeGotIP(wifiGotIpCb);
    WiFi.onStationModeDHCPTimeout(wifiDhcpTimeoutCb);

    tryNextAp();
}


DataUploader::~DataUploader()
{
    WiFi.forceSleepBegin();

    assert(instance == this);
    instance = nullptr;
}


bool DataUploader::isDone()
{
    switch(state) {
        case DataUploaderState::TRYING_ACCESS_POINT:
        case DataUploaderState::WIFI_TBD:
            switch(WiFi.status()) {
                case WL_CONNECTED:
                    if( doUpload() ) {
                        state = DataUploaderState::SUCCESS;
                        Serial.println("Uploaded successfully!");
                        return true;
                    } else {
                        // TODO: Retry?
                        Serial.println("Upload failed.");
                        tryNextAp();
                        return false;
                    }

                case WL_NO_SSID_AVAIL:  // Requested SSID not seen
                case WL_CONNECT_FAILED:
                case WL_CONNECTION_LOST:
                    tryNextAp();
                    return false;

                case WL_DISCONNECTED: // In this state while connecting
                    if( --connectCountdown == 0 ) {
                        tryNextAp();
                        return false;
                    }
                default:
                    return false;
            }
            return false;

        case DataUploaderState::SUCCESS:
        case DataUploaderState::CANT_CONNECT_TO_ANY:
            return true;

        default:
            assert(false);
            return false;
    }
}


bool DataUploader::succeeded() const
{
    switch(state) {
        case DataUploaderState::SUCCESS:
            return true;

        case DataUploaderState::CANT_CONNECT_TO_ANY:
            return false;

        default:
            return false;
    }
}


// Assumes state is TRYING_ACCESS_POINT on entry
void DataUploader::tryNextAp()
{
    if( nextAPIndex == -1 ) {
        Serial.print("Trying to connect to ");
        Serial.println(requestedSSID);
        WiFi.begin(requestedSSID.c_str(), requestedPassphrase.c_str());

        connectCountdown = DATAUPLOADER_WIFI_CONNECT_TIMEOUT;
        ++nextAPIndex;
    } else if( nextAPIndex < sizeof(staticAPs) / sizeof(staticAPs[0]) ) {
        Serial.print("Trying to connect to ");
        Serial.println(staticAPs[nextAPIndex].ssid);
        WiFi.begin( staticAPs[nextAPIndex].ssid,
                    staticAPs[nextAPIndex].passphrase );

        connectCountdown = DATAUPLOADER_WIFI_CONNECT_TIMEOUT;
        ++nextAPIndex;
    } else {
        Serial.println("Out of APs; failed");
        state = DataUploaderState::CANT_CONNECT_TO_ANY;
    }
}


bool DataUploader::doUpload()
{
#if DATAUPLOADER_USE_HTTPS
  #error "This isn't implemented yet..."
#else
    WiFiClient client;
#endif // #if/else DATAUPLOADER_USE_HTTPS

    if( !client.connect( DATAUPLOADER_SERVER_URL,
                         DATAUPLOADER_SERVER_PORT ) ) {
        Serial.println("connect() failed.");
        return false;
    }
    Serial.println("connect() success.");

    client.println("POST /upload/ HTTP/1.1");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.print("Content-Length: ");
    client.println(uploadDataLen, DEC);
    client.println("");
    client.write_P(uploadDataPtr, uploadDataLen);
    //TODO: Loop until we've written enough bytes, etc
    client.stop();

    return true;
}


/*static*/ void DataUploader::wifiConnectCb(
        const WiFiEventStationModeConnected &eventInfo )
{
    instance->state = DataUploaderState::WIFI_TBD;
    Serial.println("WiFi connected");
}


/*static*/ void DataUploader::wifiDisconnectCb(
        const WiFiEventStationModeDisconnected &eventInfo )
{
    instance->state = DataUploaderState::WIFI_TBD;
    Serial.println("WiFi disconnected");
}


/*static*/ void DataUploader::wifiAuthChangedCb(
        const WiFiEventStationModeAuthModeChanged &eventInfo )
{
    instance->state = DataUploaderState::WIFI_TBD;
    Serial.println("WiFi auth mode changed");
}


/*static*/ void DataUploader::wifiGotIpCb(
        const WiFiEventStationModeGotIP &eventInfo)
{
    instance->state = DataUploaderState::WIFI_TBD;
    Serial.println("WiFi got IP");
}


/*static*/ void DataUploader::wifiDhcpTimeoutCb(void)
{
    instance->state = DataUploaderState::WIFI_TBD;
    Serial.println("WiFi DHCP timeout");
}

