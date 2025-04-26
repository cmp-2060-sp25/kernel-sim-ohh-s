#include "linked_list.h"

void initList(linked_list* list, size_t dataSize) {
    list->head = NULL, list->tail = NULL;
    list->dataSize = dataSize;
    list->size = 0;
}

void append(linked_list* list, void* item) {
    ListNode *newNode = (ListNode*)malloc(sizeof(ListNode));
    newNode->data = malloc(list->dataSize);
    memcpy(newNode->data, item, list->dataSize);
    newNode->next = NULL;

    if (isListEmpty(list))
        list->head = list->tail = newNode;
    else {
        list->tail->next = newNode;
        list->tail = newNode;
    }

    list->size++;
}


int isListEmpty(linked_list* list) {
    return list->head == NULL;
}

void* removeFront(linked_list* list) {
    if(isListEmpty(list)) return NULL;

    ListNode* temp = list->head;
    void *data = temp->data;

    list->head = list->head->next;
    if(isListEmpty(list)) list->tail = NULL;

    free(temp);
    list->size--;

    return data;
}

void* getFront(linked_list* list) {
    if(isListEmpty(list)) return NULL;
    return list->head->data;
}

void clearList(linked_list* list) {
    while (!isListEmpty(list))
    {
        void* data = getFront(list);
        free(data);
    }
}


// Endpoints for deque.h
void prepend(linked_list* list, void* item) {
    ListNode* newNode = (ListNode*)malloc(sizeof(ListNode));
    newNode->data = malloc(list->dataSize);
    memcpy(newNode->data, item, list->dataSize);
    newNode->next = list->head;

    list->head = newNode;
    if (!list->tail) list->tail = newNode;
    list->size++;
}

void* removeBack(linked_list* list) {
    if (isListEmpty(list)) return NULL;

    ListNode* curr = list->head;
    ListNode* prev = NULL;

    while (curr->next) {
        prev = curr;
        curr = curr->next;
    }

    void* data = curr->data;

    if (prev) {
        prev->next = NULL;
        list->tail = prev;
    } else {
        list->head = list->tail = NULL;
    }

    free(curr);
    list->size--;
    return data;
}

void* getBack(linked_list* list) {
    return list->tail ? list->tail->data : NULL;
}

