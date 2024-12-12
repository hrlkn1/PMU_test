#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <arpa/inet.h>

#define MAX_PMUS 100
#define PI 3.141592653589793

// PMU Data Structure
typedef struct {
    char name[50];
    double frequency;    // Frequency in Hz
    double rocof;        // Rate of Change of Frequency
    double magnitude;    // Magnitude of phasor
    double angle;        // Phase angle in degrees
    uint16_t status;     // Digital status word
    pthread_t thread_id; // Thread ID
    bool active;
    int socket_fd;       // Socket for sending data
    struct sockaddr_in dest_addr; // Destination address
} PMU;

// Global Variables
PMU pmus[MAX_PMUS];
int pmu_count = 0;
bool running = true;

// Timestamp Helper
void get_timestamp(uint16_t* timestamp_high, uint32_t* timestamp_low) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *timestamp_high = (uint16_t)(ts.tv_sec >> 16);   // High 16 bits of seconds
    *timestamp_low = (uint32_t)(ts.tv_sec & 0xFFFF); // Low 32 bits of seconds
    *timestamp_low |= (uint32_t)(ts.tv_nsec / 1000); // Add nanoseconds as microseconds
}

// Function to simulate PMU data and send IEEE C37.118 Data Frames
void* pmu_stream(void* arg) {
    PMU* pmu = (PMU*)arg;
    while (pmu->active) {
        // Simulate data
        pmu->angle += 1.0; // Increment phase angle
        if (pmu->angle >= 360.0) pmu->angle -= 360.0;
        pmu->magnitude = 1.0 + 0.1 * sin(pmu->angle * PI / 180.0);
        pmu->frequency = 50.0 + 0.05 * cos(pmu->angle * PI / 180.0);
        pmu->rocof = 0.01 * sin(pmu->angle * PI / 180.0);

        // Build IEEE C37.118 Data Frame
        uint16_t sync = htons(0xAA01); // Sync word for Data Frame
        uint16_t frame_size = htons(18); // Fixed frame size for simplicity
        uint16_t idcode = htons(1); // PMU ID
        uint16_t timestamp_high;
        uint32_t timestamp_low;
        get_timestamp(&timestamp_high, &timestamp_low);
        uint16_t stat = htons(pmu->status);
        uint32_t phasor_real = htonl((uint32_t)(pmu->magnitude * cos(pmu->angle * PI / 180.0) * 1000));
        uint32_t phasor_imag = htonl((uint32_t)(pmu->magnitude * sin(pmu->angle * PI / 180.0) * 1000));
        uint16_t freq = htons((uint16_t)((pmu->frequency - 50.0) * 1000 + 5000)); // Offset to center frequency at 50Hz
        uint16_t dfreq = htons((uint16_t)(pmu->rocof * 1000));

        // Create buffer and populate data
        uint8_t buffer[18];
        memcpy(buffer, &sync, 2);
        memcpy(buffer + 2, &frame_size, 2);
        memcpy(buffer + 4, &idcode, 2);
        memcpy(buffer + 6, &timestamp_high, 2);
        memcpy(buffer + 8, &timestamp_low, 4);
        memcpy(buffer + 12, &stat, 2);
        memcpy(buffer + 14, &phasor_real, 4);
        memcpy(buffer + 18, &phasor_imag, 4);
        memcpy(buffer + 22, &freq, 2);
        memcpy(buffer + 24, &dfreq, 2);

        // Send data to network
        sendto(pmu->socket_fd, buffer, 18, 0, (struct sockaddr*)&pmu->dest_addr, sizeof(pmu->dest_addr));

        sleep(1); // Simulate data rate
    }
    return NULL;
}

// Add PMU
void add_pmu(const char* name, const char* ip, int port) {
    if (pmu_count >= MAX_PMUS) {
        printf("Max PMU limit reached.\n");
        return;
    }
    PMU* pmu = &pmus[pmu_count++];
    strncpy(pmu->name, name, 50);
    pmu->frequency = 50.0;
    pmu->rocof = 0.0;
    pmu->magnitude = 1.0;
    pmu->angle = 0.0;
    pmu->status = 0x0000;
    pmu->active = true;

    // Initialize socket
    pmu->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (pmu->socket_fd < 0) {
        perror("Socket creation failed");
        return;
    }

    // Setup destination address
    pmu->dest_addr.sin_family = AF_INET;
    pmu->dest_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &pmu->dest_addr.sin_addr);

    // Start thread
    pthread_create(&pmu->thread_id, NULL, pmu_stream, pmu);
    printf("PMU %s added and started, sending to %s:%d.\n", name, ip, port);
}

// Main function
int main() {
    char command[100];
    printf("PMU Simulator started. Type 'stop' to exit.\n");

    while (running) {
        printf("> ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        command[strcspn(command, "\n")] = 0;

        if (strncmp(command, "addPMU", 6) == 0) {
            char name[50], ip[50];
            int port;
            sscanf(command + 7, "%s %s %d", name, ip, &port);
            add_pmu(name, ip, port);
        } else if (strcmp(command, "stop") == 0) {
            running = false;
        } else {
            printf("Unknown command: %s\n", command);
        }
    }

    // Cleanup
    for (int i = 0; i < pmu_count; ++i) {
        pmus[i].active = false;
        pthread_join(pmus[i].thread_id, NULL);
    }
    printf("PMU Simulator stopped.\n");
    return 0;
}
