#include "utility.h"

void findOptimalCombination()
{
    // pthread_mutex_lock(&listMutex);

    // Desactivar todas las centrales primero
    HydroelectricPlantNode *node = head;
    while (node)
    {
        node->plant.isActive = 0;
        node = node->next;
    }

    float bestGeneration = 0.0;
    HydroelectricPlantNode *bestNode = NULL;

    // Probar todas las combinaciones posibles
    for (node = head; node != NULL; node = node->next)
    {
        float currentGeneration = 0.0;
        HydroelectricPlantNode *tempNode = head;
        while (tempNode != NULL)
        {
            if ((tempNode == node ||
                 (tempNode->plant.waterLevel > tempNode->plant.minWaterLevel &&
                  tempNode->plant.waterLevel < tempNode->plant.maxWaterLevel)) &&
                currentGeneration + tempNode->plant.capacity <= MAX_GENERATION)
            {
                currentGeneration += tempNode->plant.capacity;
            }
            tempNode = tempNode->next;
        }

        if (currentGeneration >= MIN_GENERATION && currentGeneration <= MAX_GENERATION && currentGeneration > bestGeneration)
        {
            bestGeneration = currentGeneration;
            bestNode = node;
        }
    }

    // Activar las centrales de la mejor combinación encontrada
    if (bestNode != NULL)
    {
        node = head;
        while (node != NULL)
        {
            if (node == bestNode ||
                (node->plant.waterLevel > node->plant.minWaterLevel &&
                 node->plant.waterLevel < node->plant.maxWaterLevel))
            {
                node->plant.isActive = 1;
            }
            node = node->next;
        }
    }

    // pthread_mutex_unlock(&listMutex);
}

void insertSorted(HydroelectricPlant plant)
{
    HydroelectricPlantNode *newNode = malloc(sizeof(HydroelectricPlantNode));
    newNode->plant = plant;
    newNode->next = NULL;

    pthread_mutex_lock(&listMutex);

    if (head == NULL || comparePlants(plant, head->plant) > 0)
    {
        newNode->next = head;
        head = newNode;
    }
    else
    {
        HydroelectricPlantNode *current = head;
        while (current->next != NULL && comparePlants(plant, current->next->plant) <= 0)
        {
            current = current->next;
        }
        newNode->next = current->next;
        current->next = newNode;
    }

    pthread_mutex_unlock(&listMutex);
}

int comparePlants(HydroelectricPlant a, HydroelectricPlant b)
{
    // Primero comparar por capacidad
    if (a.capacity != b.capacity)
    {
        return (a.capacity > b.capacity) ? -1 : 1;
    }
    // Luego por nivel de agua
    if (a.waterLevel != b.waterLevel)
    {
        return (a.waterLevel > b.waterLevel) ? -1 : 1;
    }
    // Si son iguales en ambos criterios
    return 0;
}

void sortList()
{
    if (!head || !head->next)
    {
        return;
    }

    HydroelectricPlantNode *sorted = NULL;
    HydroelectricPlantNode *current = head;

    while (current != NULL)
    {
        HydroelectricPlantNode *next = current->next;

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

        current = next;
    }

    head = sorted;
}