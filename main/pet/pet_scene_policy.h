#pragma once

// local_hour=-1 means the clock has not been synchronised. Match the UI clock;
// do not add another timezone offset to the time received from the server.
inline bool UseNightPetScene(bool sleeping, int local_hour) {
    return sleeping || (local_hour >= 0 && local_hour < 8) ||
           (local_hour >= 19 && local_hour <= 23);
}
