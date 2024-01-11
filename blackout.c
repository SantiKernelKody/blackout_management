#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>

// Definiciones de constantes
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

// Estructura para las centrales hidroeléctricas
typedef struct
{
    char *name;
    float capacity;
    float minWaterLevel;
    float maxWaterLevel;
    float waterLevel;
    pthread_t thread;
    int isActive;
} HydroelectricPlant;

typedef struct HydroelectricPlantNode
{
    HydroelectricPlant plant;
    struct HydroelectricPlantNode *next;
} HydroelectricPlantNode;

// Variables globales
HydroelectricPlantNode *head = NULL;
pthread_mutex_t listMutex, energyMutex;
sem_t adjustmentSemaphore, sortingSemaphore;
float probA, probB, probC;
float totalEnergyGenerated = 0.0;

// Prototipos de funciones
void *hydroelectricPlantRoutine(void *arg);
void *sortingThreadRoutine(void *arg);
void applyGreedyAlgorithm();
void insertSorted(HydroelectricPlant plant);
int comparePlants(HydroelectricPlant a, HydroelectricPlant b);
void sortList();
void findOptimalCombination();

int main(int argc, char *argv[])
{
    if (argc != 7)
    {
        fprintf(stderr, "Uso: %s <Prob A> <Prob B> <Prob C> <Num H1> <Num H2> <Num H3>\n", argv[0]);
        return 1;
    }

    probA = atof(argv[1]);
    probB = atof(argv[2]);
    probC = atof(argv[3]);
    int numH1 = atoi(argv[4]);
    int numH2 = atoi(argv[5]);
    int numH3 = atoi(argv[6]);

    if (probA + probB + probC != 1.0f)
    {
        fprintf(stderr, "Error: la suma de las probabilidades debe ser 1.\n");
        return 1;
    }

    pthread_mutex_init(&listMutex, NULL);
    pthread_mutex_init(&energyMutex, NULL);
    sem_init(&adjustmentSemaphore, 0, 0);
    sem_init(&sortingSemaphore, 0, 0);

    // Crear y añadir centrales a la lista
    // ... [Crear centrales y llamar a insertSorted para cada una] ...
    for (int i = 0; i < numH1; ++i)
    {
        HydroelectricPlant h1 = {"H1", 15.0, 50.0, 200.0, (50.0 + 200.0) / 2, 0, 0};
        insertSorted(h1);
    }
    for (int i = 0; i < numH2; ++i)
    {
        HydroelectricPlant h2 = {"H2", 5.0, 25.0, 100.0, (25.0 + 100.0) / 2, 0, 0};
        insertSorted(h2);
    }
    for (int i = 0; i < numH3; ++i)
    {
        HydroelectricPlant h3 = {"H3", 2.0, 10.0, 50.0, (10.0 + 50.0) / 2, 0, 0};
        insertSorted(h3);
    }

    applyGreedyAlgorithm();

    // Crear y lanzar hilos de centrales
    HydroelectricPlantNode *current = head;
    while (current)
    {
        pthread_create(&current->plant.thread, NULL, hydroelectricPlantRoutine, &current->plant);
        current = current->next;
    }

    // Crear y lanzar el hilo de ordenamiento
    pthread_t sortingThread;
    pthread_create(&sortingThread, NULL, sortingThreadRoutine, NULL);

    while (1)
    {
        sem_wait(&adjustmentSemaphore);
        applyGreedyAlgorithm();
        sem_post(&sortingSemaphore);
    }

    // Liberar recursos
    pthread_mutex_destroy(&listMutex);
    sem_destroy(&adjustmentSemaphore);
    sem_destroy(&sortingSemaphore);
    return 0;
}

void *hydroelectricPlantRoutine(void *arg)
{
    HydroelectricPlant *plant = (HydroelectricPlant *)arg;
    int rainDuration = 0;
    float rainIncrement = 0.0;

    while (1)
    {
        if (rainDuration > 0)
        {
            // Incrementar el nivel del agua debido a la lluvia
            plant->waterLevel += rainIncrement;
            rainDuration--;
        }
        else
        {
            // Simular un nuevo evento de lluvia
            float prob = (float)rand() / RAND_MAX;
            if (prob < probA)
            { // No lluvia
                rainIncrement = NO_RAIN_INCREMENT;
                rainDuration = NO_RAIN_DURATION;
            }
            else if (prob < probA + probB)
            { // Aguacero
                rainIncrement = AGUACERO_INCREMENT;
                rainDuration = AGUACERO_DURATION;
            }
            else
            { // Diluvio
                rainIncrement = DILUVIO_INCREMENT;
                rainDuration = DILUVIO_DURATION;
            }
        }

        // Simulación de la operación de la central
        if (plant->isActive)
        {
            pthread_mutex_lock(&energyMutex);
            totalEnergyGenerated += plant->capacity;
            pthread_mutex_unlock(&energyMutex);

            // Reducir el nivel del agua debido a la generación de energía
            plant->waterLevel -= 5.0;

            // Desactivar la central si el nivel de agua está fuera de los límites
            if (plant->waterLevel < plant->minWaterLevel || plant->waterLevel > plant->maxWaterLevel)
            {
                plant->isActive = 0;
                sem_post(&adjustmentSemaphore); // Enviar señal para ajustar las centrales
            }
        }

        sleep(1); // Esperar un segundo antes de la próxima iteración
    }
    return NULL;
}

void *sortingThreadRoutine(void *arg)
{
    while (1)
    {
        sem_wait(&sortingSemaphore);
        pthread_mutex_lock(&listMutex);
        sortList();
        pthread_mutex_unlock(&listMutex);
    }
    return NULL;
}

void applyGreedyAlgorithm()
{
    // pthread_mutex_lock(&listMutex);

    float currentGeneration = 0.0;
    HydroelectricPlantNode *currentNode = head;

    while (currentNode != NULL)
    {
        if (!currentNode->plant.isActive &&
            currentNode->plant.waterLevel > currentNode->plant.minWaterLevel &&
            currentGeneration + currentNode->plant.capacity <= MAX_GENERATION)
        {
            currentNode->plant.isActive = 1;
            currentGeneration += currentNode->plant.capacity;
        }
        currentNode = currentNode->next;
    }

    if (currentGeneration < MIN_GENERATION || currentGeneration > MAX_GENERATION)
    {
        findOptimalCombination();
    }

    // pthread_mutex_unlock(&listMutex);
}
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
