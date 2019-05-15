// ZDI commands, used from both ARM and ESP builds

    case 'v': // show chip version
        printf("%02x", zdiIn(1));
        printf(".%02x", zdiIn(0));
        printf(".%02x\n", zdiIn(2));
        break;
    case 'i': // initialise ZDI pins
        RST = 1; RST.mode(Pinmode::out);
        ZCL = 1; ZCL.mode(Pinmode::out);
        ZDA = 0; ZDA.mode(Pinmode::out);
        //RST = 0; wait_ms(2); RST = 1;
        break;
    case 'b': // break
        zdiOut(0x10, 0x80);
        break;
    case 'c': // continue
        zdiOut(0x10, 0x00);
        break;
    case 'h': // halt
        zIns(0x76);
        break;
    case 'n': // nop
        zIns(0x00);
        break;
    case 'R': // reset
        zdiOut(0x11, 0x80);
        ZCL = 1; // enable ZDI
        ZDA = 0; // start in break mode
        break;
    case 'H': // hardware reset
        ezReset();
        break;
    case 'a': // set ADL
        zCmd(0x08);
        break;
    case 'z': // reset ADL
        zCmd(0x09);
        break;
    case 'r': // register dump
        dumpReg();
        break;
    case 's': // detect mem size
        printf("%d KB\n", memSizer());
        break;
    case 'j': // jump (also needs 'c')
        setPC(0xFFE000);
        break; 

    case 't': // test internal 16 KB RAM
        memoryTest(0xFFC000, 0x4000);
        break;
    case 'T': { // test external 512..2048 KB ram in banks of 64 KB
        //uint32_t t = ticks;
        memoryTest(0x200000, 0x200000);
        //printf("%d ms\n", ticks - t);
        break;
    }
    case 'B': // test walking bits 0..7, to detect data bit problems
        // internal ram, 4 KB at 0xFFF000
        printf("Testing internal ram bits 0..7:\n");
        for (int i = 0; i < 8; ++i) {
            printf("  bit %d @ ", i);
            memoryTest(0xFFF000, 0x1000, 1<<i);
        }
        // external ram, 4 KB at 0x200000
        printf("Testing EXTERNAL ram bits 0..7:\n");
        for (int i = 0; i < 8; ++i) {
            printf("  bit %d @ ", i);
            memoryTest(0x200000, 0x1000, 1<<i);
        }
        break;
    case 'N': // no wait states for external ram (default is 7)
        zIns(0x3E,0x08); // ld a,08h
        zIns(0xED, 0x39, 0xAA); // out0 (0AAh),a
        break;

    case 'm': { // memory dump
        uint8_t buf [16];
        for (unsigned addr = 0; addr < 64; addr += 16) {
            readMem(0xFFE000 + addr, buf, sizeof buf);
            for (unsigned i = 0; i < sizeof buf; ++i)
                printf(" %02x", buf[i]);
            printf("\n");
        }
    }
    break;

    case '0': setMbase(0x00); break;
    case '1': setMbase(0x20); break;
    case '2': setMbase(0xFF); break;
