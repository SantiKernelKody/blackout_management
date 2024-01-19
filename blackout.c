// Require for thread affinity
#define _GNU_SOURCE
// Headers
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <sched.h>

// Colors definition
const char *c_red = "\033[31m";
const char *c_green = "\033[32m";
const char *c_blue = "\033[34m";
const char *c_magenta = "\033[35m";
const char *c_white = "\033[37m";
const char *c_yellow = "\033[33m";
const char *c_cian = "\033[36m";
const char *c_end = "\033[0m";
const char *c_orange = "\033[0;33m";
const char *c_pink = "\033[95m";

// Constants
const float H1_CAPACITY = 15.0;
const float H2_CAPACITY = 5.0;
const float H3_CAPACITY = 2.0;
const float MIN_GENERATION = 100.0;
const float MAX_GENERATION = 150.0;
const float NO_RAIN_INCREMENT = 0.0;
const float AGUACERO_INCREMENT = 2.0; // Incremento para Aguacero
const float DILUVIO_INCREMENT = 4.0;  // Incremento para Diluvio
const int NO_RAIN_DURATION = 0;
const int AGUACERO_DURATION = 10; // Duración de Aguacero
const int DILUVIO_DURATION = 5;   // Duración de Diluvio
volatile sig_atomic_t shutdownRequested = 0;

// HydroelectricPlantNode structure
typedef struct
{
    char *name;
    float capacity;
    float minWaterLevel;
    float maxWaterLevel;
    float waterLevel;
    int isActive;
    pthread_t thread;
} HydroelectricPlant;

typedef struct HydroelectricPlantNode
{
    HydroelectricPlant *plant;
    struct HydroelectricPlantNode *next;
} HydroelectricPlantNode;

// Gobal variables
HydroelectricPlantNode *head = NULL;
pthread_mutex_t listMutex, energyMutex;
sem_t adjustmentSemaphore, sortingSemaphore;
float probA, probB, probC;
float totalEnergyGenerated = 0.0;
int lastShots = 4;
bool waitingForRecover = false;

// Functions definition
void createAndInsertPlants(int numPlants, const char *plantType, float capacity, float minWaterLevel, float maxWaterLevel);
void *hydroelectricPlantRoutine(void *arg);
void *sortingThreadRoutine();
bool applyGreedyAlgorithm();
void insertSorted(HydroelectricPlant *plant);
int comparePlants(HydroelectricPlant *a, HydroelectricPlant *b);
void sortList();
void activatePlant(HydroelectricPlant *plant);
void deactivatePlant(HydroelectricPlant *plant);
void shutdownPlantsAndPrintFinalStatus();
/**
 * Handles system signals.
 * Specifically handles the SIGINT signal (Ctrl+C interruption).
 * When SIGINT is received, it sets the shutdownRequested flag to 1
 * indicating that a graceful shutdown of the program is requested.
 *
 * @param sig The signal received.
 */
void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        shutdownRequested = 1;
        printf("%s\nShutting down simulation - shutdown signal received***\n%s", c_red, c_end);
    }
}
/**
 * Main function of the program.
 * It initializes and manages the simulation of hydroelectric plants.
 *
 * @param argc The count of command-line arguments.
 * @param argv The command-line arguments.
 * @return int Returns 0 on successful execution, 1 on error.
 */
int main(int argc, char *argv[])
{
    // Validate the correct number of input arguments
    if (argc != 7)
    {
        fprintf(stderr, "Usage: %s <Prob A> <Prob B> <Prob C> <Num H1> <Num H2> <Num H3>\n", argv[0]);
        return 1;
    }

    // Parse the input arguments
    probA = atof(argv[1]);
    probB = atof(argv[2]);
    probC = atof(argv[3]);
    int numH1 = atoi(argv[4]);
    int numH2 = atoi(argv[5]);
    int numH3 = atoi(argv[6]);

    // Ensure the sum of probabilities is equal to 1.0
    if (probA + probB + probC != 1.0f)
    {
        fprintf(stderr, "Error: The sum of probabilities must be 1.\n");
        return 1;
    }

    // Calculate the maximum total capacity
    float totalMaxCapacity = numH1 * H1_CAPACITY + numH2 * H2_CAPACITY + numH3 * H3_CAPACITY;

    // Validate if the total capacity is sufficient
    if (totalMaxCapacity < MIN_GENERATION)
    {
        fprintf(stderr, "Error: The maximum total capacity of %f MW/s does not reach the required minimum of %f MW/s.\n", totalMaxCapacity, MIN_GENERATION);
        return 1;
    }

    // Signal handler setup
    struct sigaction sa;
    sa.sa_handler = &signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Initialize mutexes and semaphores
    pthread_mutex_init(&energyMutex, NULL);
    sem_init(&adjustmentSemaphore, 0, 0);
    sem_init(&sortingSemaphore, 0, 0);

    // Create and add power plants to the list
    createAndInsertPlants(numH1, "H1", H1_CAPACITY, 50.0, 200.0);
    createAndInsertPlants(numH2, "H2", H2_CAPACITY, 25.0, 100.0);
    createAndInsertPlants(numH3, "H3", H3_CAPACITY, 10.0, 50.0);

    // Apply Greedy algorithm to determine active plants before thread creation
    applyGreedyAlgorithm();

    // Create and launch threads for power plants with CPU affinity
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN); // Get the number of CPU cores
    HydroelectricPlantNode *current = head;
    int core_id = 0;
    while (current)
    {
        pthread_attr_t attr;
        cpu_set_t cpus;
        pthread_attr_init(&attr);
        CPU_ZERO(&cpus);
        CPU_SET(core_id % num_cores, &cpus); // Assign the thread to a specific core
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
        pthread_create(&current->plant->thread, &attr, hydroelectricPlantRoutine, current->plant);
        pthread_attr_destroy(&attr); // Clean thread attributes after use
        core_id++;
        current = current->next;
    }

    // Create and launch the sorting thread
    pthread_t sortingThread;
    pthread_create(&sortingThread, NULL, sortingThreadRoutine, NULL);

    // Main loop for Greedy algorithm and semaphore for Sorting thread
    while (!shutdownRequested)
    {
        sem_wait(&adjustmentSemaphore);
        printf("%sCapacity adjustment required: %f MW/s%s\n", c_yellow, totalEnergyGenerated, c_end);
        applyGreedyAlgorithm();

        printf("%sAdjusted capacity: %f MW/s%s\n", c_green, totalEnergyGenerated, c_end);
        sem_post(&sortingSemaphore);
    }

    // Wait for all threads to finish
    current = head;
    while (current)
    {
        pthread_join(current->plant->thread, NULL);
        current = current->next;
    }
    pthread_join(sortingThread, NULL);

    // Free resources
    sem_destroy(&adjustmentSemaphore);
    sem_destroy(&sortingSemaphore);

    current = head;
    while (current)
    {
        free(current->plant);
        HydroelectricPlantNode *temp = current;
        current = current->next;
        free(temp);
    }
    return 0;
}

/**
 * Creates and inserts a specified number of hydroelectric plants into the linked list.
 * Each plant is initialized with the given capacity, minimum and maximum water levels.
 * The plants are named according to their type and index, ensuring unique identifiers.
 *
 * @param numPlants The number of plants to create.
 * @param plantType The type of the plant, used as a part of the plant's name.
 * @param capacity The capacity of each plant in megawatts.
 * @param minWaterLevel The minimum water level for each plant.
 * @param maxWaterLevel The maximum water level for each plant.
 */
void createAndInsertPlants(int numPlants, const char *plantType, float capacity, float minWaterLevel, float maxWaterLevel)
{
    for (int i = 0; i < numPlants; ++i)
    {
        // Allocate memory for a new plant
        HydroelectricPlant *plant = malloc(sizeof(HydroelectricPlant));
        if (plant == NULL)
        {
            fprintf(stderr, "Error: Could not allocate memory for the power plants.\n");
            exit(-1); // Exit the program entirely if memory allocation fails
        }

        // Generate and assign a unique name for the plant
        char nameBuffer[15];
        sprintf(nameBuffer, "ID_%d_%s", i, plantType);
        plant->name = strdup(nameBuffer);

        // Initialize plant properties
        plant->capacity = capacity;
        plant->minWaterLevel = minWaterLevel;
        plant->maxWaterLevel = maxWaterLevel;
        plant->waterLevel = (minWaterLevel + maxWaterLevel) / 2;
        plant->isActive = 0;

        // Insert the plant into the sorted list
        insertSorted(plant);
    }
}

/**
 * The routine for each hydroelectric plant thread. It simulates the operation of a hydroelectric plant,
 * including changes in water levels due to rain events and energy generation. The routine also handles
 * the activation and deactivation of the plant based on water levels.
 *
 * @param arg A pointer to a HydroelectricPlant structure.
 * @return Returns NULL upon completion.
 */
void *hydroelectricPlantRoutine(void *arg)
{
    HydroelectricPlant *plant = (HydroelectricPlant *)arg;
    int rainDuration = 0;
    float rainIncrement = 0.0;
    char *rain_type = "NL"; // No Rain

    while (!shutdownRequested)
    {
        // Handle ongoing rain event
        if (rainDuration > 0)
        {
            plant->waterLevel += rainIncrement;
            rainDuration--;
        }
        else
        {
            // Simulate a new rain event
            float prob = (float)rand() / RAND_MAX;
            if (prob < probA) // No rain
            {
                rainIncrement = NO_RAIN_INCREMENT;
                rainDuration = NO_RAIN_DURATION;
                rain_type = "NL";
            }
            else if (prob < probA + probB) // Light rain
            {
                rainIncrement = AGUACERO_INCREMENT;
                rainDuration = AGUACERO_DURATION;
                rain_type = "AG";
            }
            else // Heavy rain
            {
                rainIncrement = DILUVIO_INCREMENT;
                rainDuration = DILUVIO_DURATION;
                rain_type = "DI";
            }
        }

        // Simulate plant operation if active and not in recovery mode
        if (plant->isActive && !waitingForRecover)
        {
            plant->waterLevel -= 5.0;             // Reduce water level due to energy generation
            float water_flow = rainIncrement - 5; // Calculate net water flow
            printf("%s %s %s Central %s - water_level: %.2f - water_flow: %.2f m/s.\n", c_cian, rain_type, c_end, plant->name, plant->waterLevel, water_flow);

            // Deactivate plant if water level is out of bounds
            if (plant->waterLevel <= plant->minWaterLevel || plant->waterLevel >= plant->maxWaterLevel)
            {
                deactivatePlant(plant);
                sem_post(&adjustmentSemaphore);
                printf("%sDeactivating plant %s.%s\n", c_red, plant->name, c_end);
            }
        }

        // Handle excess water if plant is inactive
        if (!plant->isActive && plant->waterLevel > plant->maxWaterLevel)
        {
            plant->waterLevel -= 5.0;
        }

        sleep(1); // Wait one second before next iteration
    }

    pthread_exit(NULL); // Clean up and orderly exit the thread
    return NULL;
}

/**
 * The routine for the sorting thread. This function continuously sorts the list of hydroelectric plants
 * based on certain criteria. It waits for a signal before performing the sorting operation.
 * The sorting is executed until a shutdown request is received.
 *
 * @return Returns NULL upon completion.
 */
void *sortingThreadRoutine()
{
    while (!shutdownRequested)
    {
        // Notify about sorting operation execution
        printf("%sExecuting sorting thread.%s\n", c_magenta, c_end);
        // Sort the list of hydroelectric plants
        sortList();
        // Wait for a signal to start the sorting
        sem_wait(&sortingSemaphore);
    }

    // Clean up and orderly exit the thread
    pthread_exit(NULL);
    return NULL;
}

/**
 * Deactivates a given hydroelectric plant.
 * This function sets the active status of the plant to false (0) and updates the total energy
 * generated by reducing the capacity of the deactivated plant.
 *
 * @param plant A pointer to the HydroelectricPlant structure representing the plant to be deactivated.
 */
void deactivatePlant(HydroelectricPlant *plant)
{
    plant->isActive = 0;
    pthread_mutex_lock(&energyMutex);        // Lock the mutex to ensure exclusive access to shared resource
    totalEnergyGenerated -= plant->capacity; // Update the total energy generation
    pthread_mutex_unlock(&energyMutex);      // Unlock the mutex
}

/**
 * Activates a given hydroelectric plant.
 * This function sets the active status of the plant to true (1) and updates the total energy
 * generated by adding the capacity of the activated plant.
 *
 * @param plant A pointer to the HydroelectricPlant structure representing the plant to be activated.
 */
void activatePlant(HydroelectricPlant *plant)
{
    plant->isActive = 1;
    pthread_mutex_lock(&energyMutex);        // Lock the mutex to ensure exclusive access to shared resource
    totalEnergyGenerated += plant->capacity; // Update the total energy generation
    pthread_mutex_unlock(&energyMutex);      // Unlock the mutex
}

/**
 * Inserts a hydroelectric plant into the global linked list in a sorted order.
 * The sorting is based on the comparison criteria defined in the comparePlants function.
 * The function allocates memory for a new HydroelectricPlantNode, assigns the provided plant to it,
 * and inserts it into the list such that the list remains sorted.
 *
 * @param plant A pointer to the HydroelectricPlant structure to be inserted into the list.
 */
void insertSorted(HydroelectricPlant *plant)
{
    HydroelectricPlantNode *newNode = malloc(sizeof(HydroelectricPlantNode)); // Allocate memory for new node
    newNode->plant = plant;                                                   // Assign the plant to the new node
    newNode->next = NULL;                                                     // Set the next pointer of the new node to NULL

    // Insert the new node into the list in the sorted position
    if (head == NULL || comparePlants(newNode->plant, head->plant) > 0) // If the list is empty or new node should be the new head
    {
        newNode->next = head; // Set the new node's next to current head
        head = newNode;       // Update the head to the new node
    }
    else // Insert the new node at the correct sorted position in the list
    {
        HydroelectricPlantNode *current = head;
        // Traverse the list to find the correct position to insert the new node
        while (current->next != NULL && comparePlants(newNode->plant, current->next->plant) <= 0)
        {
            current = current->next;
        }
        newNode->next = current->next; // Set the new node's next to current node's next
        current->next = newNode;       // Insert the new node after the current node
    }
}
/**
 * Compares two hydroelectric plants based on their capacity and relative water levels.
 * The comparison is primarily based on the capacity of the plants, and secondarily on
 * their water levels relative to their minimum and maximum limits.
 *
 * @param a Pointer to the first HydroelectricPlant to compare.
 * @param b Pointer to the second HydroelectricPlant to compare.
 * @return An integer greater than 0 if 'a' has higher priority than 'b', less than 0 if 'b' has higher priority, or 0 if they are equal.
 */
int comparePlants(HydroelectricPlant *a, HydroelectricPlant *b)
{
    // Compare based on capacity
    if (a->capacity != b->capacity)
    {
        return (a->capacity > b->capacity) ? 1 : -1;
    }
    // Then compare based on relative water level
    if (a->waterLevel != b->waterLevel)
    {
        float relative_level_a = (a->waterLevel - a->minWaterLevel) / (a->maxWaterLevel - a->minWaterLevel);
        float relative_level_b = (b->waterLevel - b->minWaterLevel) / (b->maxWaterLevel - b->minWaterLevel);
        return (relative_level_a > relative_level_b) ? 1 : -1;
    }
    return 0;
}

/**
 * Sorts the global linked list of hydroelectric plants based on the criteria defined in comparePlants function.
 * The sorting is implemented using a modified insertion sort algorithm, suitable for linked lists.
 */
void sortList()
{
    if (!head || !head->next)
    {
        return; // Nothing to sort if the list is empty or has only one element
    }

    HydroelectricPlantNode *sorted = NULL;  // Sorted part of the list
    HydroelectricPlantNode *current = head; // Pointer to the current element in the original list

    while (current != NULL)
    {
        HydroelectricPlantNode *next = current->next; // Store the next element

        // Insert the current element into the correct position in the sorted part of the list
        if (sorted == NULL || comparePlants(current->plant, sorted->plant) > 0)
        {
            current->next = sorted;
            sorted = current;
        }
        else
        {
            HydroelectricPlantNode *sortedCurrent = sorted;
            while (sortedCurrent->next != NULL && comparePlants(current->plant, sortedCurrent->next->plant) <= 0)
            {
                sortedCurrent = sortedCurrent->next;
            }
            current->next = sortedCurrent->next;
            sortedCurrent->next = current;
        }

        current = next; // Move to the next element in the original list
    }

    head = sorted; // Update the head of the list to the sorted list
}

/**
 * Applies a greedy algorithm to activate hydroelectric plants optimally.
 * The algorithm iterates through the list of plants and activates them if doing so doesn't exceed
 * the maximum generation capacity and if the plant's water level is above its minimum.
 * It aims to reach at least the minimum generation capacity. If the minimum capacity isn't reached,
 * it attempts to recover by recursively calling itself, decrementing a counter each time.
 * If the recovery attempts run out, it triggers a shutdown sequence.
 *
 * @return A boolean indicating whether a satisfactory generation level was achieved (true) or not (false).
 */
bool applyGreedyAlgorithm()
{
    printf("Applying greedy algorithm.\n");

    float currentGeneration = totalEnergyGenerated;
    HydroelectricPlantNode *currentNode = head;

    // Activate plants optimally
    while (currentNode != NULL)
    {
        if (!currentNode->plant->isActive && currentNode->plant->waterLevel > currentNode->plant->minWaterLevel &&
            currentGeneration + currentNode->plant->capacity <= MAX_GENERATION)
        {
            activatePlant(currentNode->plant);
            printf("%sActivated Plant %s%s\n", c_blue, currentNode->plant->name, c_end);
            currentGeneration += currentNode->plant->capacity;
        }
        if (currentGeneration >= MIN_GENERATION)
        {
            waitingForRecover = false;
            return true; // Stop if minimum generation is reached
        }
        currentNode = currentNode->next;
    }
    if (lastShots > 1)
    {
        waitingForRecover = true;
        lastShots -= 1;
        printf("%sALERT: starting recovery attempts before immediate shutdown - %i.%s\n", c_red, lastShots, c_end);
        sleep(1);
        return applyGreedyAlgorithm();
    }
    else
    {
        shutdownPlantsAndPrintFinalStatus();
    }
    return false;
}

/**
 * Shuts down all hydroelectric plants and prints their final status. This function is called when no
 * combination of plant activations can maintain the required energy generation levels. It iterates
 * through all the plants, printing their final water level and activation status before signaling
 * the program to shut down.
 */
void shutdownPlantsAndPrintFinalStatus()
{
    printf("%sNo combination can maintain the plants operational. Proceeding to shut down everything.%s\n", c_red, c_end);
    printf("%sBelow is the final state of each plant.%s\n", c_red, c_end);
    HydroelectricPlantNode *current = head;

    while (current != NULL)
    {
        HydroelectricPlant *plant = current->plant;
        printf("Plant: %s, Min Water Level: %.2f, Max Water Level: %.2f, Current Water Level: %.2f, Status: %s\n",
               plant->name,
               plant->minWaterLevel,
               plant->maxWaterLevel,
               plant->waterLevel,
               plant->isActive ? "Activated" : "Deactivated");
        current = current->next;
    }
    shutdownRequested = 1;
}