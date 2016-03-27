AddressInfo(CLIENT 139.165.222.16) // Sam
AddressInfo(ENTRY 139.165.222.16) // Sam
AddressInfo(PROC0 139.165.222.19) // Merry
AddressInfo(PROC1 139.165.222.20) // Pippin
define($entry_mac_out_0 1:1:1:1:1:1) // Fake MAC
define($entry_mac_out_1 90:e2:ba:46:f2:d4) // Sam ens2f0
define($entry_dev_out_1 2)
define($entry_mac_out_2 a0:36:9f:2c:59:60) // Sam ens6f0
define($entry_dev_out_2 0)
define($client_mac_in_0 2:2:2:2:2:2) // Fake MAC
define($client_mac_in_1 90:e2:ba:46:f2:d5) // Sam ens2f1
define($client_dev_in_1 3)
define($client_mac_in_2 a0:36:9f:2c:59:62) // Sam ens6f1
define($client_dev_in_2 1)
define($proc0_mac_in a0:36:9f:29:46:9e) // Merry ens6f1
define($proc0_dev_in 1)
define($proc0_mac_out a0:36:9f:29:46:9c) // Merry ens6f0
define($proc0_dev_out 0)
define($proc1_mac_in 90:e2:ba:46:f2:e1) // Pippin ens2f1
define($proc1_dev_in 1)
define($proc1_mac_out 90:e2:ba:46:f2:e0) // Pippin ens2f0
define($proc1_dev_out 0)

define($pblock true)

define($ignore 0) //ingnore first X seconds of receive, to get stable data
define($replay_count 1)
define($file 201601-1_fullanon_padded.pcap)
define($n_seconds 10)
define($sleep 0)
define($checksum true)

DPDKInfo(700000)

source :: {
    fromDump :: FromDump("/mnt/traces/$file", STOP false,
                         END_AFTER $n_seconds);

    // We use Replay as a memory buffer, but we play packets only once
    replay :: Replay(STOP $replay_count, QUICK_CLONE false, SLEEP $sleep);
    StaticThreadSched(replay 0);

    // We need Unqueue, because replay has pull output
    unqueue :: Unqueue(BURST 32);
    StaticThreadSched(unqueue 1);

    counter :: AverageCounter(IGNORE $ignore);

    // Output 0 is IP, output 1 is ARP, output 2 is other traffic
    ethClassifier :: Classifier(12/0800, 12/0806, -);

    fromDump -> EnsureDPDKBuffer -> EnsureHeadroom(48) -> replay -> unqueue -> ethClassifier;

    ethClassifier[0] -> counter -> Strip(14) -> CheckIPHeader() -> output;
    ethClassifier[1] -> Discard();
    ethClassifier[2] -> Print("Unknown input traffic") -> Discard();
};

receiver :: {
    counter :: AverageCounter(IGNORE $ignore);

    input -> CheckIPHeader() -> counter -> Discard();
};

entry :: PNFVEntry(N 24, BLINDS \<
    67 c6 69 73 51 ff 4a ec 29 cd ba ab f2 fb e3 46 7c c2 54 f8 1b e8 e7 8d
    76 5a 2e 63 33 9f c9 9a 66 32 0d b7 31 58 a3 5a 25 5d 05 17 58 e9 5e d4
    ab b2 cd c6 9b b4 54 11 0e 82 74 41 21 3d dc 87 70 e9 3e a1 41 e1 fc 67
    3e 01 7e 97 ea dc 6b 96 8f 38 5c 2a ec b0 3b fb 32 af 3c 54 ec 18 db 5c
    02 1a fe 43 fb fa aa 3a fb 29 d1 e6 05 3c 7c 94 75 d8 be 61 89 f9 5c bb
    a8 99 0f 95 b1 eb f1 b3 05 ef f7 00 e9 a1 3a e5 ca 0b cb d0 48 47 64 bd
    1f 23 1e a8 1c 7b 64 c5 14 73 5a c5 5e 4b 79 63 3b 70 64 24 11 9e 09 dc
    aa d4 ac f2 1b 10 af 3b 33 cd e3 50 48 47 15 5c bb 6f 22 19 ba 9b 7d f5
    0b e1 1a 1c 7f 23 f8 29 f8 a4 1b 13 b5 ca 4e e8 98 32 38 e0 79 4d 3d 34
    bc 5f 4e 77 fa cb 6c 05 ac 86 21 2b aa 1a 55 a2 be 70 b5 73 3b 04 5c d3
    36 94 b3 af e2 f0 e4 9e 4f 32 15 49 fd 82 4e a9 08 70 d4 b2 8a 29 54 48
    9a 0a bc d5 0e 18 a8 44 ac 5b f3 8e 4c d7 2d 9b 09 42 e5 06 c4 33 af cd
    a3 84 7f 2d ad d4 76 47 de 32 1c ec 4a c4 30 f6 20 23 85 6c fb b2 07 04
    f4 ec 0b b9 20 ba 86 c3 3e 05 f1 ec d9 67 33 b7 99 50 a3 e3 14 d3 d9 34
    f7 5e a0 f2 10 a8 f6 05 94 01 be b4 bc 44 78 fa 49 69 e6 23 d0 1a da 69
    6a 7e 4c 7e 51 25 b3 48 84 53 3a 94 fb 31 99 90 32 57 44 ee 9b bc e9 e5
    25 cf 08 f5 e9 e2 5e 53 60 aa d2 b2 d0 85 fa 54 d8 35 e8 d4 66 82 64 98
    d9 a8 87 75 65 70 5a 8a 3f 62 80 29 44 de 7c a5 89 4e 57 59 d3 51 ad ac
    86 95 80 ec 17 e4 85 f1 8c 0c 66 f1 7c c0 7c bb 22 fc e4 66 da 61 0b 63
    af 62 bc 83 b4 69 2f 3a ff af 27 16 93 ac 07 1f b8 6d 11 34 2d 8d ef 4f
    89 d4 b6 63 35 c1 c7 e4 24 83 67 d8 ed 96 12 ec 45 39 02 d8 e5 0a f8 9d
    77 09 d1 a5 96 c1 f4 1f 95 aa 82 ca 6c 49 ae 90 cd 16 68 ba ac 7a a6 f2
    b4 a8 ca 99 b2 c2 37 2a cb 08 cf 61 c9 c3 80 5e 6e 03 28 da 4c d7 6a 19
    ed d2 d3 99 4c 79 8b 00 22 56 9a d4 18 d1 fe e4 d9 cd 45 a3 91 c6 01 ff
    c9 2a d9 15 01 43 2f ee 15 02 87 61 7c 13 62 9e 69 fc 72 81 cd 71 65 a6
    3e ab 49 cf 71 4b ce 3a 75 a7 4f 76 ea 7e 64 ff 81 eb 61 fd fe c3 9b 67
    bf 0d e9 8c 7e 4e 32 bd f9 7c 8c 6a c7 5b a4 3c 02 f4 b2 ed 72 16 ec f3
    01 4d f0 00 10 8b 67 cf 99 50 5b 17 9f 8e d4 98 0a 61 03 d1 bc a7 0d be
    9b bf ab 0e d5 98 01 d6 e5 f2 d6 f6 7d 3e c5 16 8e 21 2e 2d af 02 c6 b9
    63 c9 8a 1f 70 97 de 0c 56 89 1a 2b 21 1b 01 07 0d d8 fd 8b 16 c2 a1 a4
    e3 cf d2 92 d2 98 4b 35 61 d5 55 d1 6c 33 dd c2 bc f7 ed de 13 ef e5 20
    c7 e2 ab dd a4 4d 81 88 1c 53 1a ee eb 66 24 4c 3b 79 1e a8 ac fb 6a 68
    f3 58 46 06 47 2b 26 0e 0d d2 eb b2 1f 6c 3a 3b c0 54 2a ab ba 4e f8 f6
    c7 16 9e 73 11 08 db 04 60 22 0a a7 4d 31 b5 5b 03 a0 0d 22 0d 47 5d cd
    9b 87 78 56 d5 70 4c 9c 86 ea 0f 98 f2 eb 9c 53 0d a7 fa 5a d8 b0 b5 db
    50 c2 fd 5d 09 5a 2a a5 e2 a3 fb b7 13 47 54 9a 31 63 32 23 4e ce 76 5b
    75 71 b6 4d 21 6b 28 71 2e 25 cf 37 80 f9 dc 62 9c d7 19 b0 1e 6d 4a 4f
    d1 7c 73 1f 4a e9 7b c0 5a 31 0d 7b 9c 36 ed ca 5b bc 02 db b5 de 3d 52
    b6 57 02 d4 c4 4c 24 95 c8 97 b5 12 80 30 d2 db 61 e0 56 fd 16 43 c8 71
    ff ca 4d b5 a8 8a 07 5e e1 09 33 a6 55 57 3b 1d ee f0 2f 6e 20 02 49 81
    e2 a0 7f f8 e3 47 69 e3 11 b6 98 b9 41 9f 18 22 a8 4b c8 fd a2 04 1a 90
    f4 49 fe 15 4b 48 96 2d e8 15 25 cb 5c 8f ae 6d 45 46 27 86 e5 3f a9 8d
    8a 71 8a 2c 75 a4 bc 6a ee ba 7f 39 02 15 67 ea 2b 8c b6 87 1b 64 f5 61
    ab 1c e7 90 5b 90 1e e5 02 a8 11 77 4d cd e1 3b 87 60 74 8a 76 db 74 a1
    68 2a 28 83 8f 1d e4 3a 39 cc ca 94 5c e8 79 5e 91 8a d6 de 57 b7 19 df
    18 8d 69 8e 69 dd 2f d1 08 57 54 97 75 39 d1 ae 05 9b 43 61 84 bc c0 15
    47 96 f3 9e 4d 0c 7d 65 99 e6 f3 02 c4 22 d3 cc 7a 28 63 ef 61 34 9d 66
    cf e0 c7 53 9d 87 68 e4 1d 5b 82 6b 67 00 d0 01 e6 c4 03 aa e6 d7 76 60
    ff d9 4f 60 0d ed c6 dd cd 8d 30 6a 15 99 4e 32 f4 d1 9d 5c d1 6e 5d b7
    32 60 62 18 37 d8 79 36 b2 c8 96 bf b5 5c 9c 83 ea cd ed ff 66 3c 31 5a
    0d cf b6 de 3d 13 95 6f 74 f7 87 ab d0 00 e2 82 c9 78 41 7e d5 de 01 bf
    ab ef be 11 2b ef 6b 38 be 22 16 fb 35 ab 6a a9 a3 f2 55 73 f2 37 f5 bb
    af 36 3a 84 14 3b 43 bf 2a 01 d0 55 f1 3c 8d af 5e a3 ab 93 4f 15 3d f2
    07 92 65 fa c9 5a b5 78 90 ef fd a5 2b 40 64 55 42 35 ab 33 71 38 e2 cf
    dc 8d 62 2b a3 9f 1d aa 31 82 a4 fa dc 5a 73 6c 49 70 11 74 b0 76 ca f2
    ab 75 25 1c ad 08 eb 89 95 4d b4 38 ed d1 e3 1e 53 87 19 2f e1 8c 9c 2b
    fc ad 9f ac 23 69 9f ce de c4 ea 8c cc d5 15 62 23 ca 9a 10 9b 7d 2e ef
    05 47 1e e6 d3 ba 11 cf 68 b1 7c 8b 1a 1b 5a f9 df 44 85 ac 1a 9a 0e 3d
    64 a8 4d 00 26 7b ef 2b c3 0d 11 96 c8 23 66 30 d4 e2 bb ee fd 15 e7 dc
    5a 6c 88 74 07 96 b1 6b 3f fe 6b 65 79 5a 90 3c 68 a1 d3 30 c4 39 60 98
    1b 1b 87 18 31 6e f4 8b db 7d ff e2 13 b0 4d 52 ae b9 b7 27 13 47 64 7b
    e9 37 ab ad 70 0b 46 8b 27 cd a3 58 3b 97 e3 16 14 e2 f8 28 92 46 7a 40
    ff 32 67 12 79 cb 8e 62 02 39 10 72 45 56 fd 6c 23 a0 c4 5e 38 a7 75 4c
    89 6d 74 1b b3 ef 5b b2 21 c2 c5 9a 8e 53 fd 90 8c 0d 03 d1 63 00 3d 86>);

client :: PNFVClient(N 24, BLINDS \<
    67 c6 69 73 51 ff 4a ec 29 cd ba ab f2 fb e3 46 7c c2 54 f8 1b e8 e7 8d
    76 5a 2e 63 33 9f c9 9a 66 32 0d b7 31 58 a3 5a 25 5d 05 17 58 e9 5e d4
    ab b2 cd c6 9b b4 54 11 0e 82 74 41 21 3d dc 87 70 e9 3e a1 41 e1 fc 67
    3e 01 7e 97 ea dc 6b 96 8f 38 5c 2a ec b0 3b fb 32 af 3c 54 ec 18 db 5c
    02 1a fe 43 fb fa aa 3a fb 29 d1 e6 05 3c 7c 94 75 d8 be 61 89 f9 5c bb
    a8 99 0f 95 b1 eb f1 b3 05 ef f7 00 e9 a1 3a e5 ca 0b cb d0 48 47 64 bd
    1f 23 1e a8 1c 7b 64 c5 14 73 5a c5 5e 4b 79 63 3b 70 64 24 11 9e 09 dc
    aa d4 ac f2 1b 10 af 3b 33 cd e3 50 48 47 15 5c bb 6f 22 19 ba 9b 7d f5
    0b e1 1a 1c 7f 23 f8 29 f8 a4 1b 13 b5 ca 4e e8 98 32 38 e0 79 4d 3d 34
    bc 5f 4e 77 fa cb 6c 05 ac 86 21 2b aa 1a 55 a2 be 70 b5 73 3b 04 5c d3
    36 94 b3 af e2 f0 e4 9e 4f 32 15 49 fd 82 4e a9 08 70 d4 b2 8a 29 54 48
    9a 0a bc d5 0e 18 a8 44 ac 5b f3 8e 4c d7 2d 9b 09 42 e5 06 c4 33 af cd
    a3 84 7f 2d ad d4 76 47 de 32 1c ec 4a c4 30 f6 20 23 85 6c fb b2 07 04
    f4 ec 0b b9 20 ba 86 c3 3e 05 f1 ec d9 67 33 b7 99 50 a3 e3 14 d3 d9 34
    f7 5e a0 f2 10 a8 f6 05 94 01 be b4 bc 44 78 fa 49 69 e6 23 d0 1a da 69
    6a 7e 4c 7e 51 25 b3 48 84 53 3a 94 fb 31 99 90 32 57 44 ee 9b bc e9 e5
    25 cf 08 f5 e9 e2 5e 53 60 aa d2 b2 d0 85 fa 54 d8 35 e8 d4 66 82 64 98
    d9 a8 87 75 65 70 5a 8a 3f 62 80 29 44 de 7c a5 89 4e 57 59 d3 51 ad ac
    86 95 80 ec 17 e4 85 f1 8c 0c 66 f1 7c c0 7c bb 22 fc e4 66 da 61 0b 63
    af 62 bc 83 b4 69 2f 3a ff af 27 16 93 ac 07 1f b8 6d 11 34 2d 8d ef 4f
    89 d4 b6 63 35 c1 c7 e4 24 83 67 d8 ed 96 12 ec 45 39 02 d8 e5 0a f8 9d
    77 09 d1 a5 96 c1 f4 1f 95 aa 82 ca 6c 49 ae 90 cd 16 68 ba ac 7a a6 f2
    b4 a8 ca 99 b2 c2 37 2a cb 08 cf 61 c9 c3 80 5e 6e 03 28 da 4c d7 6a 19
    ed d2 d3 99 4c 79 8b 00 22 56 9a d4 18 d1 fe e4 d9 cd 45 a3 91 c6 01 ff
    c9 2a d9 15 01 43 2f ee 15 02 87 61 7c 13 62 9e 69 fc 72 81 cd 71 65 a6
    3e ab 49 cf 71 4b ce 3a 75 a7 4f 76 ea 7e 64 ff 81 eb 61 fd fe c3 9b 67
    bf 0d e9 8c 7e 4e 32 bd f9 7c 8c 6a c7 5b a4 3c 02 f4 b2 ed 72 16 ec f3
    01 4d f0 00 10 8b 67 cf 99 50 5b 17 9f 8e d4 98 0a 61 03 d1 bc a7 0d be
    9b bf ab 0e d5 98 01 d6 e5 f2 d6 f6 7d 3e c5 16 8e 21 2e 2d af 02 c6 b9
    63 c9 8a 1f 70 97 de 0c 56 89 1a 2b 21 1b 01 07 0d d8 fd 8b 16 c2 a1 a4
    e3 cf d2 92 d2 98 4b 35 61 d5 55 d1 6c 33 dd c2 bc f7 ed de 13 ef e5 20
    c7 e2 ab dd a4 4d 81 88 1c 53 1a ee eb 66 24 4c 3b 79 1e a8 ac fb 6a 68
    f3 58 46 06 47 2b 26 0e 0d d2 eb b2 1f 6c 3a 3b c0 54 2a ab ba 4e f8 f6
    c7 16 9e 73 11 08 db 04 60 22 0a a7 4d 31 b5 5b 03 a0 0d 22 0d 47 5d cd
    9b 87 78 56 d5 70 4c 9c 86 ea 0f 98 f2 eb 9c 53 0d a7 fa 5a d8 b0 b5 db
    50 c2 fd 5d 09 5a 2a a5 e2 a3 fb b7 13 47 54 9a 31 63 32 23 4e ce 76 5b
    75 71 b6 4d 21 6b 28 71 2e 25 cf 37 80 f9 dc 62 9c d7 19 b0 1e 6d 4a 4f
    d1 7c 73 1f 4a e9 7b c0 5a 31 0d 7b 9c 36 ed ca 5b bc 02 db b5 de 3d 52
    b6 57 02 d4 c4 4c 24 95 c8 97 b5 12 80 30 d2 db 61 e0 56 fd 16 43 c8 71
    ff ca 4d b5 a8 8a 07 5e e1 09 33 a6 55 57 3b 1d ee f0 2f 6e 20 02 49 81
    e2 a0 7f f8 e3 47 69 e3 11 b6 98 b9 41 9f 18 22 a8 4b c8 fd a2 04 1a 90
    f4 49 fe 15 4b 48 96 2d e8 15 25 cb 5c 8f ae 6d 45 46 27 86 e5 3f a9 8d
    8a 71 8a 2c 75 a4 bc 6a ee ba 7f 39 02 15 67 ea 2b 8c b6 87 1b 64 f5 61
    ab 1c e7 90 5b 90 1e e5 02 a8 11 77 4d cd e1 3b 87 60 74 8a 76 db 74 a1
    68 2a 28 83 8f 1d e4 3a 39 cc ca 94 5c e8 79 5e 91 8a d6 de 57 b7 19 df
    18 8d 69 8e 69 dd 2f d1 08 57 54 97 75 39 d1 ae 05 9b 43 61 84 bc c0 15
    47 96 f3 9e 4d 0c 7d 65 99 e6 f3 02 c4 22 d3 cc 7a 28 63 ef 61 34 9d 66
    cf e0 c7 53 9d 87 68 e4 1d 5b 82 6b 67 00 d0 01 e6 c4 03 aa e6 d7 76 60
    ff d9 4f 60 0d ed c6 dd cd 8d 30 6a 15 99 4e 32 f4 d1 9d 5c d1 6e 5d b7
    32 60 62 18 37 d8 79 36 b2 c8 96 bf b5 5c 9c 83 ea cd ed ff 66 3c 31 5a
    0d cf b6 de 3d 13 95 6f 74 f7 87 ab d0 00 e2 82 c9 78 41 7e d5 de 01 bf
    ab ef be 11 2b ef 6b 38 be 22 16 fb 35 ab 6a a9 a3 f2 55 73 f2 37 f5 bb
    af 36 3a 84 14 3b 43 bf 2a 01 d0 55 f1 3c 8d af 5e a3 ab 93 4f 15 3d f2
    07 92 65 fa c9 5a b5 78 90 ef fd a5 2b 40 64 55 42 35 ab 33 71 38 e2 cf
    dc 8d 62 2b a3 9f 1d aa 31 82 a4 fa dc 5a 73 6c 49 70 11 74 b0 76 ca f2
    ab 75 25 1c ad 08 eb 89 95 4d b4 38 ed d1 e3 1e 53 87 19 2f e1 8c 9c 2b
    fc ad 9f ac 23 69 9f ce de c4 ea 8c cc d5 15 62 23 ca 9a 10 9b 7d 2e ef
    05 47 1e e6 d3 ba 11 cf 68 b1 7c 8b 1a 1b 5a f9 df 44 85 ac 1a 9a 0e 3d
    64 a8 4d 00 26 7b ef 2b c3 0d 11 96 c8 23 66 30 d4 e2 bb ee fd 15 e7 dc
    5a 6c 88 74 07 96 b1 6b 3f fe 6b 65 79 5a 90 3c 68 a1 d3 30 c4 39 60 98
    1b 1b 87 18 31 6e f4 8b db 7d ff e2 13 b0 4d 52 ae b9 b7 27 13 47 64 7b
    e9 37 ab ad 70 0b 46 8b 27 cd a3 58 3b 97 e3 16 14 e2 f8 28 92 46 7a 40
    ff 32 67 12 79 cb 8e 62 02 39 10 72 45 56 fd 6c 23 a0 c4 5e 38 a7 75 4c
    89 6d 74 1b b3 ef 5b b2 21 c2 c5 9a 8e 53 fd 90 8c 0d 03 d1 63 00 3d 86>,
    BUFSIZE 262144);

pipe0 :: Pipeliner(BLOCKING $pblock);
StaticThreadSched(pipe0 2);
pipe1 :: Pipeliner(BLOCKING $pblock);
StaticThreadSched(pipe1 3);
pipe2 :: Pipeliner(BLOCKING $pblock);
StaticThreadSched(pipe2 4);

pipe0b :: Pipeliner(BLOCKING $pblock);
StaticThreadSched(pipe0b 10);

source -> entry;
entry[0] -> pipe0 -> UDPIPEncap(ENTRY, 5678, CLIENT, 5678, CHECKSUM $checksum)
    -> EtherEncap(0x0800, $entry_mac_out_0, $client_mac_in_0) -> Strip(14)
	-> pipe0b
    -> CheckIPHeader() -> CheckUDPHeader()
    -> IPFilter(allow udp && dst port 5678, drop all) -> Strip(28)
    -> [0]client;
entry[1] -> pipe1 -> UDPIPEncap(ENTRY, 5678, PROC0, 5678, CHECKSUM $checksum)
    -> EtherEncap(0x0800, $entry_mac_out_1, $proc0_mac_in)
    -> ToDPDKDevice($entry_dev_out_1, BLOCKING $pblock);
entry[2] -> pipe2 -> UDPIPEncap(ENTRY, 5678, PROC1, 5678, CHECKSUM $checksum)
    -> EtherEncap(0x0800, $entry_mac_out_2, $proc1_mac_in)
    -> ToDPDKDevice($entry_dev_out_2, BLOCKING $pblock);
FromDPDKDevice($client_dev_in_1,MAXTHREADS 1) -> Strip(14) -> CheckIPHeader()
    -> CheckUDPHeader() -> IPFilter(allow udp && dst port 5678, drop all)
    -> Strip(28) -> [1]client;
FromDPDKDevice($client_dev_in_2,MAXTHREADS 1) -> Strip(14) -> CheckIPHeader()
    -> CheckUDPHeader() -> IPFilter(allow udp && dst port 5678, drop all)
    -> Strip(28) -> [2]client;
client -> receiver;

DriverManager(pause,
	      wait 2s,
              print "Packet Rate:",
	      print $(source/counter.rate),
	      print $(receiver/counter.rate),
	      print "Link Rate:",
	      print $(source/counter.link_rate),
	      print $(receiver/counter.link_rate),
	      print "Count :",
	      print $(source/counter.count),
	      print $(receiver/counter.count),
	      stop);
