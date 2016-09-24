#ifndef DATA_UPLOADER_HEADER
#define DATA_UPLOADER_HEADER

#include "HouseSensor.h"

/// Connects to the WiFi AP, uploads data to server, etc.
/*!
 * The DataUploader has a static list of "known" access points, which it will
 * attempt to use if there is a problem using the user-specified access point
 * (or if none is provided).
 */
class DataUploader
{
    public:
        DataUploader(APCredentials *preferredAP);
        ~DataUploader();

        /// Advances state machine, returns true when we're done
        bool isDone();

    protected:
        /// Set to the user's preferred AP, or empty string if using the list.
        String requestedSSID, requestedPassword;

        enum class DataUploaderState {

        };
    
}; // end class DataUploader

#endif // #ifndef DATA_UPLOADER_HEADER

