#ifndef UTILITY_H
#define UTILITY_H

#include <semaphore.h>

// Definición de la estructura HydroelectricPlant
typedef struct
{
    char *name;
    float capacity;
    float minWaterLevel;
    float maxWaterLevel;
    float waterLevel;
    pthread_t thread;
    int isActive;
    sem_t sem;
} HydroelectricPlant;
typedef struct HydroelectricPlantNode
{
    HydroelectricPlant plant;
    struct HydroelectricPlantNode *next;
} HydroelectricPlantNode;

// Prototipos de funciones de utilidad
void insertSorted(HydroelectricPlant plant);
int comparePlants(HydroelectricPlant a, HydroelectricPlant b);
void sortList();
void findOptimalCombination();

#endif // UTILITY_H
