// CHANGES

    // tcp_packet* arrPackets[] = NULL;

    // while file is not empty:
    //    read len data from file
    //    make packet with len data
    //    append packet to list of packets

    // make an array of packets

    // size of file to determine num of packets
    fseek(fp, 0, SEEK_END);
    int size = ftell(fp);

    // reset to beg
    fseek(fp, 0, SEEK_SET);

    // making packets in array
    int num_packs = size / DATA_SIZE;
    tcp_packet *packArr = malloc(num_packs * sizeof(tcp_packet));

    len = fread(buffer, 1, DATA_SIZE, fp); // read outside - first step

    int count = 0;

    while (len > 0)
    {
        tcp_packet *pack = make_packet(len);
        memcpy(pack->data, buffer, len);
        packArr[count] = *pack;
        count++;
        len = fread(buffer, 1, DATA_SIZE, fp);
    }

    if (count == num_packs)
    {
        printf("count == num_packets");
    }

    // CHANGES
