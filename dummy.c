void popCurrent(packet_list ** head, packet_list ** tail, packet_list * current) {
    if (current == *head) {
        pop(head);
        return;
    }
    packet_list * temp = *head;
    while (temp->next != current) {
        temp = temp->next;
    }
    temp->next = current->next;
    if (current == *tail) {
        *tail = temp;
    }
}
