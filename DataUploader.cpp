#include "DataUploader.h"

/// Used for the static list of public APs that we know about at build time
/*!
 * The loginUrl is for public access points that require clicking an "I agree"
 * button to get access.
 */
struct StaticAPInfo
{
    char *ssid;
    char *password;
    char *loginUrl;
};

#pragma GCC diagnostic push
// warning: deprecated conversion from string constant to 'char*'
#pragma GCC diagnostic ignored "-Wwrite-strings"
StaticAPInfo staticAPs[]{ {"Wicked",  "", ""},
                          {"Library", "", ""} };
#pragma GCC diagnostic pop


DataUploader::DataUploader( APCredentials *preferredAP /* = NULL */ )
{
}


DataUploader::~DataUploader()
{
}


bool DataUploader::isDone()
{
    return false;
}

