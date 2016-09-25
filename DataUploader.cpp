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

DataUploader * DataUploader::instance(nullptr);

DataUploader::DataUploader( APCredentials *preferredAP /* = nullptr */ )
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
                    // TODO Handle actually uploading data
                    state = DataUploaderState::SUCCESS;
                    Serial.println("Got connection in isDone()");
                    return true;

                case WL_CONNECT_FAILED:
                case WL_CONNECTION_LOST:
                    tryNextAp();
                    return false;

                case WL_DISCONNECTED: // In this state while connecting
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

        ++nextAPIndex;
    } else if( nextAPIndex < sizeof(staticAPs) / sizeof(staticAPs[0]) ) {
        Serial.print("Trying to connect to ");
        Serial.println(staticAPs[nextAPIndex].ssid);
        WiFi.begin( staticAPs[nextAPIndex].ssid,
                    staticAPs[nextAPIndex].passphrase );

        ++nextAPIndex;
    } else {
        Serial.println("Out of APs; failed");
        state = DataUploaderState::CANT_CONNECT_TO_ANY;
    }
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

