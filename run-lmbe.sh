make clean && make 
./ssd tracefile/LMBE.trace
./ssd --gcsync --ndisk 4 --diskid 0 --gc_time_window 2000 tracefile/LMBE.trace
./ssd --gcsync --ndisk 4 --diskid 0 --gc_time_window 5000 tracefile/LMBE.trace
./ssd --gcsync --ndisk 4 --diskid 0 --gc_time_window 10000 tracefile/LMBE.trace
./ssd --gcsync --ndisk 4 --diskid 0 --gc_time_window 50000 tracefile/LMBE.trace
./ssd --gcsync --ndisk 4 --diskid 0 --gc_time_window 100000 tracefile/LMBE.trace
