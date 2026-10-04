root@ing-wyze-cam3-a000 opt# ./streamer
[I 840605741] MODULE: Initializing module system
[I 840606691] MODULE: Module system initialized successfully
[I 840607479] MODULE: Registered module 'rtsp' v1.0.0 (RTSP streaming server module)
[D 840608402] main: Starting streamer - PID: 3437
[I 840608841] main: Channel metrics initialized (legacy mode)
[D 840609204] CONFIG: Using config file /opt/streamer.json
[I 840609457] CONFIG: Loading configuration from: /opt/streamer.json
[I 840610144] CONFIG: JSON configuration loaded successfully
[D 840610457] CONFIG: Parsing JSON configuration values...
[D 840610977] CONFIG: Parsed JPEG config: enabled=true, quality=85, channel=2
[D 840611241] CONFIG: Found streams array at root level
[D 840611479] CONFIG: Found streams array with 2 elements
[D 840611708] CONFIG: Allocated streams array for 2 streams
[D 840612008] CONFIG: Parsed resolution '1920x1080' as 1920x1080
[D 840612225] CONFIG: Parsed stream[0]: 1920x1080, endpoint=ch0
[D 840612415] CONFIG: Parsed resolution '640x360' as 640x360
[D 840612595] CONFIG: Parsed stream[1]: 640x360, endpoint=ch1
[D 840612771] CONFIG: JSON configuration parsed successfully
[D 840613003] CONFIG:   Stream[0]: 1920x1080, bitrate=2048kbps, endpoint='ch0'
[D 840613241] CONFIG:   Stream[1]: 640x360, bitrate=512kbps, endpoint='ch1'
[I 840613573] CONFIG: Configuration monitoring thread started
[I 840613883] CONFIG: Configuration initialized successfully
[D 840614117] main: Streamer configuration loaded successfully
[W 840614362] MODULE: Module system already initialized
[I 840614595] main: Module system initialized
[I 840616027] MODULE: Initializing 1 registered modules
[I 840616095] MODULE: Initializing module 'rtsp'
[D 840616198] MODULE: Trying module config: /opt/rtsp.json
[I 840616763] MODULE: Loading config for module 'rtsp' from /opt/rtsp.json (binary dir)
[I 840617558] RTSP_MODULE: RTSP config loaded - port: 554, enabled: true, TLS: disabled (port: 322)
[I 840617620] RTSP_MODULE: RTSP auth config - enabled: false, username: 'admin', password: 'admin'
[I 840617958] MODULE: Module 'rtsp' config loaded successfully
[I 840618014] MODULE: Module 'rtsp' initialized successfully
[I 840618053] MODULE: Module initialization complete: 1 success, 0 errors
[I 840618496] main: Modules initialized
[D 840618551] SENSOR: Getting sensor information from /proc/jz/sensor
[D 840618767] SENSOR:   name: gc2053
[D 840619357] SENSOR:   chip_id: 0x2053
[D 840620182] SENSOR:   version: H20230726a
[D 840620648] SENSOR:   i2c_addr: 0x37
[D 840621858] SENSOR:   width: 1920
[D 840622390] SENSOR:   height: 1080
[D 840623115] SENSOR:   min_fps: 5
[D 840623771] SENSOR:   max_fps: 40
[E 840624245] SENSOR: File /proc/jz/sensor/i2c_bus not found
[E 840624569] SENSOR: Using default value 0 for i2c_bus
[E 840624945] SENSOR: File /proc/jz/sensor/boot not found
[E 840625282] SENSOR: Using default value 0 for boot
[E 840625393] SENSOR: File /proc/jz/sensor/mclk not found
[E 840625725] SENSOR: Using default value 1 for mclk
[E 840625837] SENSOR: File /proc/jz/sensor/video_interface not found
[E 840626058] SENSOR: Using default value 0 for video_interface
[E 840626167] SENSOR: File /proc/jz/sensor/reset_gpio not found
[E 840626341] SENSOR: Using default value 91 for reset_gpio
[D 840626391] SENSOR: Sensor info: gc2053 (1920x1080@40fps, i2c:0x37)
[D 840626599] main: Sensor info read successfully
[D 840626654] SENSOR: Validating configuration against sensor capabilities
[I 840626810] SENSOR: Sensor configuration: 1920x1080@20fps
[I 840626866] SENSOR: All streams will use sensor FPS: 20fps
[D 840627002] main: Configuration validated and adjusted based on sensor capabilities
[D 840627053] main: System init start
[I 840627095] main: Opening ISP...
[IMP_ISP] Open: opened /dev/tx-isp (fd=4)
[I 840624502] CONFIG: Configuration monitoring started for: /opt/streamer.json
[I 840627497] main: ISP opened successfully
[I 840627543] main: Adding sensor...
^[[C[IMP_ISP] AddSensor: enum idx=0 name='gc2053'
[IMP_ISP] AddSensor: found matching sensor at index 0
[IMP_ISP] AddSensor: using sensor_idx=0 for gc2053
[IMP_ISP] AddSensor: calling TX_ISP_SENSOR_SET_INPUT with index=0
[IMP_ISP] AddSensor: TX_ISP_SENSOR_SET_INPUT succeeded
[IMP_ISP] AddSensor: ISP buffer info: addr=0x0 size=4685424
[DMA] DMA init: using /dev/rmem
[DMA] DMA init: /dev/rmem mapped at 0x75719000 size=30408704 base_phys=0x06300000
[DMA] Alloc: (unnamed) size=4685424 phys=0x6300000 virt=0x75719000 (rmem off=0x0)
[IMP_ISP] AddSensor: allocated ISP buffer: virt=0x75719000 phys=0x6300000 size=4685424
[IMP_ISP] AddSensor: calling TX_ISP_SET_BUF with addr=0x6300000 size=4685424
[IMP_ISP] AddSensor: TX_ISP_SET_BUF succeeded
[IMP_ISP] AddSensor: gc2053 (idx=0, buf_size=4685424)
[I 840857902] main: Sensor added successfully
[I 840858137] main: Enabling sensor...
[IMP_ISP] EnableSensor: proceeding without custom AE/AWB
[IMP_ISP] EnableSensor: about to call ioctl 0x40045626
[IMP_ISP] EnableSensor: ioctl 0x40045626 returned 0
[IMP_ISP] EnableSensor: ioctl 0x40045626 succeeded, sensor_idx=0
[IMP_ISP] EnableSensor: sensor index validated, proceeding to STREAMON
[IMP_ISP] EnableSensor: calling ioctl 0x80045612 (ISP STREAMON)
[IMP_ISP] EnableSensor: ioctl 0x80045612 succeeded
[IMP_ISP] EnableSensor: calling ioctl 0x800456d0 (LINK_SETUP)
[IMP_ISP] EnableSensor: ioctl 0x800456d0 succeeded, result=0
[IMP_ISP] EnableSensor: about to call ioctl 0x800456d2 (LINK_STREAM_ON)
[IMP_ISP] EnableSensor: gISPdev=0x774eaac0, fd=4
[IMP_ISP] EnableSensor: ioctl 0x800456d2 returned 0
[IMP_ISP] EnableSensor: ioctl 0x800456d2 succeeded
[IMP_ISP] EnableSensor: incrementing opened flag
[IMP_ISP] EnableSensor: sensor enabled (idx=0)
[IMP_ISP] EnableSensor: about to return 0
[I 840859997] main: Sensor enabled successfully
[I 840860418] COMMON: System memory: 93 MB
[I 840860745] COMMON: Detected low-memory device: 93 MB total RAM
[I 840860871] COMMON: Skipping explicit memory pools - relying on reduced buffer counts
[I 840860913] COMMON: Low-memory optimizations rely on:
[I 840861028] COMMON:   - Reduced nrVBs (1 buffer per channel)
[I 840861070] COMMON:   - Smaller OSD pool (512KB)
[I 840861105] COMMON:   - Disabled unused channels
[I 840861220] COMMON:   - Let IMP system manage RMEM automatically
[I 840861262] main: Calling IMP_System_Init()...
[System] Initializing...
[System] Subsystems initialized
[System] Initialized (IMP-1.1.6)
[I 840861974] main: IMP_System_Init() returned: 0
[I 840862011] main: System initialized successfully
[IMP_ISP] EnableTuning: opened /dev/isp-m0 (fd=6)
[IMP_ISP] EnableTuning: tuning initialized successfully
[D 840862259] main: ISP tuning enabled successfully
[IMP_ISP] SetContrast: 128
[D 840862435] main: ISP tuning contrast set to 128
[IMP_ISP] SetSharpness: 128
[D 840862503] main: ISP tuning sharpness set to 128
[IMP_ISP] SetSaturation: 128
[D 840862649] main: ISP tuning saturation set to 128
[IMP_ISP] SetBrightness: 128
[D 840862715] main: ISP tuning brightness set to 128
[IMP_ISP] SetISPRunningMode: 0
[IMP_ISP] SetISPRunningMode: mode set successfully to 0
[D 840862891] main: ISP running mode set to DAY
[IMP_ISP] SetSensorFPS: 20/1
[IMP_ISP] SetSensorFPS: FPS set successfully to 20/1
[I 840865249] main: Sensor FPS set to 20/1 (all channels use sensor FPS)
[IMP_ISP] GetSensorFPS: called with fps_num=0x7ffe7300 fps_den=0x7ffe7304
[IMP_ISP] GetSensorFPS: returning 25/1
[I 840866786] main: Verified sensor FPS: 25/1 (25.00 fps)
[I 840867282] COMMON: Channel 0: enabled=1, format='H264' -> payloadType=0x1 (H264), 1920x1080@20/1fps
[I 840867547] COMMON: Channel 1: enabled=0, format='H264' -> payloadType=0x1 (H264), 640x360@20/1fps, scaler=enabled
[D 840867728] COMMON: Updated channel configuration from JSON config: ch0=1, ch1=0, ch2=0, ch3=0
[D 840867840] main: Configuration applied to channels successfully
[W 840867885] COMMON: Applying memory optimizations for low-memory device
[I 840867923] COMMON: Low-memory optimizations applied:
[I 840868040] COMMON:   - Buffer optimization: nrVBs=1, FIFO depth=0
[I 840868081] COMMON:   - Channel enabling: Based on configuration
[I 840868195] COMMON:   - Channels 2-3: Not used in current setup
[I 840868238] COMMON:   - Output resolution: Handled by RTSP module
[I 840868274] COMMON: SUCCESS: optimized with configuration-driven channel management
[FrameSource] CreateChn: Opened /dev/framechan0 (fd=7, nonblock)
[System] Registered module [0,0]: FrameSource
[FrameSource] CreateChn: registered FrameSource module [0,0] with 1 output
[FrameSource] CreateChn: chn=0, 1920x1080, fmt=0xa
[D 840868859] main: FrameSource channel 0 created
[W 840868907] main: 64MB device: Setting FIFO depth to 0
[D 840868944] main: Using buffer depth 1 for channel 0
[FrameSource] SetChnFifoAttr: chn=0, maxdepth=0, depth=0
[I 840869111] main: FrameSource channel 0: FIFO buffer depth set to 0 frames
[FrameSource] SetChnAttr: chn=0, 1920x1080, fmt=0xa
[D 840869280] main: FrameSource channel 0 attributes set
[D 840869320] main: FrameSource channel 1 not enabled
[D 840869442] main: FrameSource channel 2 not enabled
[D 840869484] main: FrameSource channel 3 not enabled
[D 840869522] main: FrameSource initialized successfully
[System] Registered module [1,0]: Encoder
[Encoder] CreateGroup: registered Encoder module [1,0] with 1 output and update callback
[Encoder] CreateGroup: grp=0
[I 840870479] main: Encoder group 0 created successfully
[I 840870546] main: Encoder group 1 not enabled
[I 840870835] main: Encoder group 2 not enabled
[I 840870886] main: Encoder group 3 not enabled
[Encoder] SetMaxStreamCnt: chn=0, cnt=2
[I 840870963] main: Channel 0: Set 2 stream buffers for low-memory device
[Encoder] SetStreamBufSize: chn=0, size=262144
[I 840871294] main: Channel 0: Set 256KB stream buffer size for low-memory device
[I 840871350] main: Channel 0: Set 2 stream buffers, 262144 bytes each
[Encoder] SetChnEntropyMode: chn=0, mode=2
[I 840871429] main: Channel 0: payloadType=0x1, computed profile=0x42
[Encoder] SetDefaultParam: 1920x1080, 20/1 fps, profile=66, rc=1
[I 840871769] main: Encoder channel 0 configured: 1920x1080@20/1fps, GOP=20, bitrate=2000kbps, mode=CBR
[Codec] SetDefaultParam: initialized
[Encoder] channel_encoder_init: 1920x1080, profile=0x42->0x42, fps=20/1, gop=20, bitrate=0
[Fifo] Init: size=4, max_elements=5
[Fifo] Init: size=7, max_elements=8
[Codec] Create: vendor library will handle hardware acceleration via /dev/avpu
[Codec] Create: codec=0x757160d0, channel=0
[Encoder] channel_encoder_init: frame_cnt=4, frame_size=1048576
[Fifo] Init: size=4, max_elements=5
[Encoder] CreateChn: chn=0, profile=66 created successfully
[Encoder] SetChnGopLength: chn=0, gop=20
[Encoder] RequestIDR: chn=0
[HW_Encoder] RequestIDR: next frame will be IDR
[Encoder] RegisterChn: grp=0, chn=0
[I 840873793] main: Encoder initialized successfully
[I 840873834] main: Initializing JPEG encoder for channel 4 (stream 0)
[Encoder] SetDefaultParam: 1920x1080, 20/1 fps, profile=67108864, rc=0
[D 840874068] main: Set default JPEG parameters for channel 4: 1920x1080
[Codec] SetDefaultParam: initialized
[Encoder] channel_encoder_init: 1920x1080, profile=0x4000000->0x0, fps=20/1, gop=0, bitrate=0
[Fifo] Init: size=4, max_elements=5
[Fifo] Init: size=7, max_elements=8
[Codec] Create: vendor library will handle hardware acceleration via /dev/avpu
[Codec] Create: codec=0x75716b80, channel=1
[Encoder] channel_encoder_init: frame_cnt=4, frame_size=1048576
[Fifo] Init: size=4, max_elements=5
[Encoder] CreateChn: chn=4, profile=67108864 created successfully
[D 840875650] main: Created JPEG channel 4
[Encoder] RegisterChn: grp=0, chn=4
[D 840875802] main: Registered JPEG channel 4 to group 0
[I 840875844] main: JPEG encoder channel 4 initialized successfully
[I 840875886] main: Stream 1 disabled, skipping JPEG encoder initialization
[I 840876008] main: Binding FrameSource -> Encoder for channel 0 (OSD disabled)
[System] Bind request: [0,0,0] -> [1,0,0]
[System] Binding [0,0,0] -> [1,0,0]
[System] Bound FrameSource -> Encoder (observer added)
[I 840876299] main: Bound FrameSource -> Encoder for channel 0
[I 840876692] main: Channel 1 not enabled, skipping bind
[I 840876732] main: Channel 2 not enabled, skipping bind
[I 840876935] main: Channel 3 not enabled, skipping bind
[I 840877144] main: Enabling FrameSource channel 0
[I 840877188] main: Channel 0: nrVBs=1, resolution=1920x1080
[FrameSource] GetChnFifoAttr: chn=0 -> maxdepth=1, depth=1
[I 840877254] main: Channel 0: FIFO maxdepth=1, type=1
[I 840877388] main: Channel 0: Expected VBM allocation ~3110400 bytes (3.0MB) for 1 buffers
[KernelIF] Set format: 1920x1080 fmt=0xa (fourcc=0x3231564e) sizeimage=3110400->3133440 bytesperline=2880 colorspace=8
[FrameSource] EnableChn: using sizeimage=3133440 from SET_FMT for chn=0
[KernelIF] Set buffer count: 4 (actual: 4)
[VBM] CreatePool: invalid frame_count=0, using default 4
[VBM] CreatePool: allocating pool_size=4640 (frame_count=4 * 0x428 + 0x180)
[Encoder] stream_thread: started for channel 0
[VBM] CreatePool: chn=0, 1920x1080 fmt=0xa, 4 frames, size=3133440
[DMA] Alloc: Lp� siz�q� size=12533760 phys=0x6778000 virt=0x75b91000 (rmem off=0x478000)
[VBM] Frame 0: phys=0x6778000 virt=0x75b91000
[VBM] Frame 1: phys=0x6a75000 virt=0x75e8e000
[VBM] Frame 2: phys=0x6d72000 virt=0x7618b000
[VBM] Frame 3: phys=0x706f000 virt=0x76488000
[VBM] CreatePool: chn=0 created successfully
[VBM] FillPool: chn=0, filling 4 frames
[VBM] FillPool: queued 4 frames
[VBM] PrimeKernelQueue: idx=0 querybuf=0, using frame size=3133440 (NV12 would be 3110400, w=1920 h=1080)
[VBM] PrimeKernelQueue: idx=1 querybuf=0, using frame size=3133440 (NV12 would be 3110400, w=1920 h=1080)
[VBM] PrimeKernelQueue: idx=2 querybuf=0, using frame size=3133440 (NV12 would be 3110400, w=1920 h=1080)
[VBM] PrimeKernelQueue: idx=3 querybuf=0, using frame size=3133440 (NV12 would be 3110400, w=1920 h=1080)
[VBM] PrimeKernelQueue: queued 4 frames to kernel for chn=0
[KernelIF] Stream started
[FrameSource] EnableChn: chn=0 enabled successfully
[Encoder] stream_thread: started for channel 4
[Encoder] encoder_thread: started for channel 4
[Encoder] encoder_thread: started for channel 0
[I 840883172] main: FrameSource channel 0 enabled successfully
[I 840883217] main: FrameSource stream started successfully
[I 840883254] main: OSD updates will be handled by OSD module
[Encoder] RequestIDR: chn=0
[HW_Encoder] RequestIDR: next frame will be IDR
[Encoder] StartRecvPic: chn=0
[I 840883530] main: Encoder channel 0 started receiving pictures
[Encoder] RequestIDR: chn=0
[HW_Encoder] RequestIDR: next frame will be IDR
[I 840883626] MODULE: Starting all initialized modules
[I 840883662] MODULE: Starting module 'rtsp'
[I 840883699] RTSP_MODULE: Starting RTSP module
[D 840883761] RTSP: Creating RTSP server on port 554
[D 840883980] RTSP: Copying RTSP server configuration
[D 840884160] RTSP: Initializing GOP cache
[D 840884300] RTSP: Initializing GOP cache for channel 0
[D 840884338] RTSP: Initializing frame buffers for channel 0
[D 840884528] RTSP: Allocating frame buffer 0 for channel 0
[D 840884972] RTSP: Allocating frame buffer 1 for channel 0
[D 840885204] RTSP: Allocating frame buffer 2 for channel 0
[D 840885270] RTSP: Allocating frame buffer 3 for channel 0
[D 840885325] RTSP: Allocating frame buffer 4 for channel 0
[D 840885506] RTSP: Allocating frame buffer 5 for channel 0
[D 840885675] RTSP: Allocating frame buffer 6 for channel 0
[D 840885738] RTSP: Allocating frame buffer 7 for channel 0
[D 840886223] RTSP: Allocating frame buffer 8 for channel 0
[D 840886558] RTSP: Allocating frame buffer 9 for channel 0
[D 840886899] RTSP: Allocating frame buffer 10 for channel 0
[D 840886994] RTSP: Allocating frame buffer 11 for channel 0
[D 840887216] RTSP: Allocating frame buffer 12 for channel 0
[D 840887302] RTSP: Allocating frame buffer 13 for channel 0
[D 840887472] RTSP: Allocating frame buffer 14 for channel 0
[D 840887529] RTSP: Allocating frame buffer 15 for channel 0
[D 840887701] RTSP: Allocating frame buffer 16 for channel 0
[D 840887863] RTSP: Allocating frame buffer 17 for channel 0
[D 840887920] RTSP: Allocating frame buffer 18 for channel 0
[D 840888088] RTSP: Allocating frame buffer 19 for channel 0
[D 840888252] RTSP: Allocating frame buffer 20 for channel 0
[D 840888308] RTSP: Allocating frame buffer 21 for channel 0
[FrameSource] frame_capture_thread: started for channel 0, state=2, fd=7
[D 840888890] RTSP: Allocating frame buffer 22 for channel 0
[D 840889495] RTSP: Allocating frame buffer 23 for channel 0
[D 840889832] RTSP: Allocating frame buffer 24 for channel 0
[D 840889892] RTSP: Allocating frame buffer 25 for channel 0
[D 840890263] RTSP: Allocating frame buffer 26 for channel 0
[D 840890346] RTSP: Allocating frame buffer 27 for channel 0
[D 840890394] RTSP: Allocating frame buffer 28 for channel 0
[D 840890726] RTSP: Allocating frame buffer 29 for channel 0
[D 840890964] RTSP: Allocating frame buffer 30 for channel 0
[D 840891342] RTSP: Allocating frame buffer 31 for channel 0
[D 840891422] RTSP: Allocating frame buffer 32 for channel 0
[D 840891472] RTSP: Allocating frame buffer 33 for channel 0
[D 840891824] RTSP: Allocating frame buffer 34 for channel 0
[D 840891896] RTSP: Allocating frame buffer 35 for channel 0
[D 840891938] RTSP: Allocating frame buffer 36 for channel 0
[D 840892000] RTSP: Allocating frame buffer 37 for channel 0
[D 840892351] RTSP: Allocating frame buffer 38 for channel 0
[D 840892416] RTSP: Allocating frame buffer 39 for channel 0
[D 840892461] RTSP: Allocating frame buffer 40 for channel 0
[D 840892792] RTSP: Allocating frame buffer 41 for channel 0
[D 840892874] RTSP: Allocating frame buffer 42 for channel 0
[D 840893252] RTSP: Allocating frame buffer 43 for channel 0
[D 840893311] RTSP: Allocating frame buffer 44 for channel 0
[D 840893356] RTSP: Allocating frame buffer 45 for channel 0
[D 840893700] RTSP: Allocating frame buffer 46 for channel 0
[D 840893762] RTSP: Allocating frame buffer 47 for channel 0
[D 840893812] RTSP: Allocating frame buffer 48 for channel 0
[D 840894340] RTSP: Allocating frame buffer 49 for channel 0
[D 840894401] RTSP: Allocating frame buffer 50 for channel 0
[D 840894442] RTSP: Allocating frame buffer 51 for channel 0
[D 840894785] RTSP: Allocating frame buffer 52 for channel 0
[D 840894863] RTSP: Allocating frame buffer 53 for channel 0
[D 840894904] RTSP: Allocating frame buffer 54 for channel 0
[D 840895246] RTSP: Allocating frame buffer 55 for channel 0
[D 840895304] RTSP: Allocating frame buffer 56 for channel 0
[D 840895354] RTSP: Allocating frame buffer 57 for channel 0
[D 840895570] RTSP: Allocating frame buffer 58 for channel 0
[D 840895621] RTSP: Allocating frame buffer 59 for channel 0
[D 840895666] RTSP: Initializing GOP cache for channel 1
[D 840895804] RTSP: Initializing frame buffers for channel 1
[D 840895841] RTSP: Allocating frame buffer 0 for channel 1
[D 840895886] RTSP: Allocating frame buffer 1 for channel 1
[D 840896050] RTSP: Allocating frame buffer 2 for channel 1
[D 840896218] RTSP: Allocating frame buffer 3 for channel 1
[D 840896268] RTSP: Allocating frame buffer 4 for channel 1
[D 840896424] RTSP: Allocating frame buffer 5 for channel 1
[D 840896474] RTSP: Allocating frame buffer 6 for channel 1
[D 840896623] RTSP: Allocating frame buffer 7 for channel 1
[D 840896674] RTSP: Allocating frame buffer 8 for channel 1
[D 840896820] RTSP: Allocating frame buffer 9 for channel 1
[D 840896872] RTSP: Allocating frame buffer 10 for channel 1
[D 840897013] RTSP: Allocating frame buffer 11 for channel 1
[D 840897159] RTSP: Allocating frame buffer 12 for channel 1
[D 840897221] RTSP: Allocating frame buffer 13 for channel 1
[D 840897382] RTSP: Allocating frame buffer 14 for channel 1
[D 840897522] RTSP: Allocating frame buffer 15 for channel 1
[D 840897577] RTSP: Allocating frame buffer 16 for channel 1
[D 840897726] RTSP: Allocating frame buffer 17 for channel 1
[D 840897778] RTSP: Allocating frame buffer 18 for channel 1
[D 840897922] RTSP: Allocating frame buffer 19 for channel 1
[D 840897976] RTSP: Allocating frame buffer 20 for channel 1
[D 840898427] RTSP: Allocating frame buffer 21 for channel 1
[D 840898516] RTSP: Allocating frame buffer 22 for channel 1
[D 840898574] RTSP: Allocating frame buffer 23 for channel 1
[D 840898748] RTSP: Allocating frame buffer 24 for channel 1
[D 840898865] RTSP: Allocating frame buffer 25 for channel 1
[D 840899052] RTSP: Allocating frame buffer 26 for channel 1
[D 840899106] RTSP: Allocating frame buffer 27 for channel 1
[D 840899262] RTSP: Allocating frame buffer 28 for channel 1
[D 840899311] RTSP: Allocating frame buffer 29 for channel 1
[D 840899462] RTSP: Allocating frame buffer 30 for channel 1
[D 840899510] RTSP: Allocating frame buffer 31 for channel 1
[D 840899680] RTSP: Allocating frame buffer 32 for channel 1
[D 840899835] RTSP: Allocating frame buffer 33 for channel 1
[D 840899892] RTSP: Allocating frame buffer 34 for channel 1
[D 840900082] RTSP: Allocating frame buffer 35 for channel 1
[D 840900248] RTSP: Allocating frame buffer 36 for channel 1
[D 840900301] RTSP: Allocating frame buffer 37 for channel 1
[D 840900454] RTSP: Allocating frame buffer 38 for channel 1
[D 840900503] RTSP: Allocating frame buffer 39 for channel 1
[D 840900656] RTSP: Allocating frame buffer 40 for channel 1
[D 840900706] RTSP: Allocating frame buffer 41 for channel 1
[D 840900874] RTSP: Allocating frame buffer 42 for channel 1
[D 840901040] RTSP: Allocating frame buffer 43 for channel 1
[D 840901090] RTSP: Allocating frame buffer 44 for channel 1
[D 840901241] RTSP: Allocating frame buffer 45 for channel 1
[D 840901290] RTSP: Allocating frame buffer 46 for channel 1
[D 840901440] RTSP: Allocating frame buffer 47 for channel 1
[D 840901709] RTSP: Allocating frame buffer 48 for channel 1
[D 840901934] RTSP: Allocating frame buffer 49 for channel 1
[D 840902076] RTSP: Allocating frame buffer 50 for channel 1
[D 840902133] RTSP: Allocating frame buffer 51 for channel 1
[D 840902302] RTSP: Allocating frame buffer 52 for channel 1
[D 840902456] RTSP: Allocating frame buffer 53 for channel 1
[D 840902512] RTSP: Allocating frame buffer 54 for channel 1
[D 840902662] RTSP: Allocating frame buffer 55 for channel 1
[D 840902710] RTSP: Allocating frame buffer 56 for channel 1
[D 840902855] RTSP: Allocating frame buffer 57 for channel 1
[D 840902912] RTSP: Allocating frame buffer 58 for channel 1
[D 840903062] RTSP: Allocating frame buffer 59 for channel 1
[D 840903114] RTSP: Initializing clients
[D 840903246] RTSP: Initializing client 0
[D 840903321] RTSP: Initializing client 1
[D 840903457] RTSP: Initializing client 2
[D 840903512] RTSP: Initializing client 3
[D 840904030] RTSP: Initializing client 4
[D 840904183] RTSP: Initializing client 5
[D 840904412] RTSP: Initializing client 6
[D 840904455] RTSP: Initializing client 7
[D 840904486] RTSP: RTSP server created successfully
[E 840904522] RTSP_MODULE: RTSP: Configuring stream 0 (ch0): 1920x1080, bitrate=2048
[Encoder] GetChnAttr: chn=0
[D 840905206] RTSP: Adding stream: ch0 (channel 0, H264)
[D 840905265] RTSP: Reallocating streams array (current count: 0)
[D 840905307] RTSP: Copying stream configuration
[D 840905344] RTSP: Stream added successfully (total streams: 1)
[Encoder] RequestIDR: chn=0
[HW_Encoder] RequestIDR: next frame will be IDR
[Encoder] StartRecvPic: chn=0
[I 840905870] RTSP: Starting RTSP server
[I 840906190] RTSP: Creating listen socket...
[I 840906275] RTSP: Listen socket created successfully
[I 840906321] RTSP: Binding socket to port 554...
[I 840906387] RTSP: Socket bound successfully to port 554
[I 840906704] RTSP: Setting socket to listen mode...
[I 840906793] RTSP: Socket listening successfully
[I 840906834] RTSP: Starting RTSP server thread...
[I 840907218] RTSP: RTSP server thread started successfully
[I 840907556] RTSP: Starting RTP thread...
[I 840907680] RTSP: RTP thread started successfully
[I 840908012] RTSP: RTSP server started successfully on port 554
[I 840908059] MODULE: Module 'rtsp' started successfully
[I 840908098] MODULE: Module start complete: 1 success, 0 errors
[I 840908136] main: Modules started
[I 840908456] SNAPSHOT_FALLBACK: Snapshot fallback initialized (dir=/tmp, interval=5000ms)
[I 840908512] SNAPSHOT_FALLBACK: HTTP module not available, starting snapshot fallback
[I 840908630] SNAPSHOT_FALLBACK: Snapshot fallback started
[I 840908852] main: Snapshot fallback system started
[I 840908959] main: Initialization complete, starting frame manager
[I 840909124] FRAME_MGR: Frame manager initialized successfully
[I 840909245] FRAME_MGR: Frame manager started successfully
[I 840909434] main: Frame manager started - using modular architecture
[I 840909654] SNAPSHOT_FALLBACK: Snapshot fallback worker thread started
[D 840909974] SNAPSHOT_FALLBACK: Capturing snapshot for channel 0 to /tmp/snap0.jpg
[Encoder] RequestIDR: chn=4
[HW_Encoder] RequestIDR: next frame will be IDR
[Encoder] StartRecvPic: chn=4
[I 840909850] FRAME_MGR: Frame processing thread started
[I 840911312] FRAME_MGR: 64MB SoC: Using aggressive polling timeout: 25ms (was 50ms)
[W 840911258] SNAPSHOT_FALLBACK: Polling JPEG stream timeout for channel 0
[Encoder] StopRecvPic: chn=4
[D 841512979] SNAPSHOT_FALLBACK: Capturing snapshot for channel 0 to /tmp/snap0.jpg


[  840.720016] gc2053 chip found @ 0x37 (i2c0)
[  840.720021] sensor driver version H20230726a
[  840.868440] gc2053 stream on
[  841.089425] ispcore: irq-status 0x00000040, err is 0x40,0x3f8,084c is 0x0
[  844.819273] gc2053 stream off
