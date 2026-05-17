window.BENCHMARK_DATA = {
  "lastUpdate": 1778984761203,
  "repoUrl": "https://github.com/yvanvds/yse-soundengine",
  "entries": {
    "libYSE benchmarks": [
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "d18f8f4dd4f643314b4b5e134a505a05372f9f76",
          "message": "CI: skip engine-dependent benchmarks in CI, add Bench/README\n\nThe first push of benchmark.yml ran past the runner timeout because the\nBM_Reverb_*, BM_Patcher_*, and BM_Engine_* benchmarks all call\nYSE::System().init(). Shared GitHub Actions runners have no audio\nhardware, so PortAudio's ALSA probe cascade emits ~30 lines of stderr,\nthe JACK fallback also fails (CAP_IPC_LOCK is not granted to non-Docker\nGHA jobs — see memory/project_ci_headless_audio.md), and the binary\ndrags on long enough to time the job out.\n\nThe previous CI-headless-audio investigation already established that\nJACK + dummy backend only works under tools/ci-linux/Dockerfile.audio\nlocally and cannot be made to work on GHA. Filter those benchmarks out\nof the CI run via\n`--benchmark_filter='-^BM_(Reverb_|Patcher_|Engine_)'`. Tier 1 DSP\nmicrobenchmarks (which is the meat of the regression-tracking suite)\nstill run; Tier 2 / macro benchmarks remain available locally.\n\nRepro'd the failure and the fix under Docker (yse-ci image) before\npushing — filtered run completes in 2 min with zero stderr noise.\n\nBench/README.md documents the split, naming convention for adding new\nbenchmarks, and the local Docker reproduction command.\n\nCo-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>",
          "timestamp": "2026-05-16T16:38:06+02:00",
          "tree_id": "9c6fe0e4a47a2d6f194e11753141e3c403829453",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/d18f8f4dd4f643314b4b5e134a505a05372f9f76"
        },
        "date": 1778942493062,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.882693790521257,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.880620441095415 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.848628557359142,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.846373157157977 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.05987000445977925,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06115267397532898 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.0050384202029624915,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005147262660104862 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 42.12578149708381,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 42.116452493501605 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 41.77800515141249,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.764546534882044 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.6308003099351853,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6278857768956633 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.014974210270232088,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.014908325362696288 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 87.11544855205216,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.11053466223764 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 87.08454807340989,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.08175151907032 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.1887600678767981,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18829792325622374 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.0021667806458462134,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0021615976068374517 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.841701085612478,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.841198615536918 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.842553368038002,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.841500538593408 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.0035645509135035017,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003448931166192017 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.0003010167954530103,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002912653759279614 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 41.76919342198644,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.76648463437808 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 41.76361900589543,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.76051461340231 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.02317502197124803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02294557028340738 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.000554835276255279,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005493775807150604 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 87.02039221014167,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.01311414185956 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 87.011620858757,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.00148761198851 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.04321809065432881,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04224195167520714 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.0004966432528821907,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00048546649653682174 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 12.173579021723178,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.17295390362908 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 12.227988248417923,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.227353102507834 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.23836173215320342,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.23843658528369563 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.019580250945745545,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.019587405585476777 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 92.79604247218002,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.79059357441893 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 92.5962777033656,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.59517942432582 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.4195310921204099,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.41571348051361595 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.004521001983960514,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0044801252422230695 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 224.2616119228558,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 224.24777480349803 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 223.8994732765566,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 223.8796822980985 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.6719817022124533,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.670969395671077 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.0029964187649003863,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0029920894254537346 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 81.53173910045673,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 81.52646796923433 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 80.86015159461284,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 80.85613840708744 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 1.1694920258961166,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1715169489637327 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.01434400932445663,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.014369774358504386 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 311.36687051901964,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 311.3456917654042 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 310.64457372017506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 310.6339576626306 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 1.3594796679370091,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3567055286766168 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00436616672053415,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004357553563641024 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 603.6419960000027,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 603.5914416666668 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 603.6761630000171,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 603.6468 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.25389361669079025,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.24659860959219446 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.0004206029705905172,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00040855219701471256 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.285892921957958,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.284805520217306 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.287809159639517,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.286386377317832 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.003321671971597018,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003517923711970903 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.00021730310349259914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00023015822525957055 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 60.538437438602536,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.53450104717172 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 60.56266679855503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.55690627040891 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.06112505045084543,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05993099180817643 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.0010096899265501827,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009900303260363043 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 123.87725533541372,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.86271464313548 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 123.81203622055635,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.7984210135764 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.11742918856949443,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11518341869401888 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0009479479364596808,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009299280984263685 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.280136079872074,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.278836747600893 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.280371089244296,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.279915053699492 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.0024547154625983825,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002729171707850236 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.00016064748702283375,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017862431236977352 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 60.406682190264206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.40259903938669 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 60.36145525611884,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.358790772984456 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.16356365409208953,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1614497202328576 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0027077079581512126,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0026728935973033404 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 125.31960020913714,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 125.30997211415303 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 123.93256842758912,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.92027229987724 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 2.4600061413934458,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.4618010228881686 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0196298594736028,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01964569125149556 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.326277080980423,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.324912225916327 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.281814349927041,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.279737993663238 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.08823181069928233,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08799582882555955 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.005756897792796407,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005742011929878965 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 60.57630040465932,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.56998370064695 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 60.54723691622394,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.54264989721266 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.16547254892162377,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.164599594326288 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.0027316384100092087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0027175109562482835 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 123.89679234399391,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.88723806520795 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 123.88988341682536,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.87997046227308 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.0387369337450428,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.039530668375700316 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.00031265485580523694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003190858799749283 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 121.40373729223734,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 121.39589355180503 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 121.36396236488763,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 121.35999017834438 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.07692737742629353,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07863315422231883 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.0006336491704626653,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006477414673731329 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 537.1608951389308,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 537.1176597937144 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 536.9784380519787,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 536.9266956793136 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.48477862974432334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4882037544887506 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.0009024830998148902,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009089326064539569 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1060.157011458708,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1060.0549756131095 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1059.9725836966952,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1059.8828216431918 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.45195607393953025,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4664915072420192 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.00042631050783474767,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004400635042274219 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 1916.0856543121329,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.92710069449 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 1916.2503646698126,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1916.0540395567589 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.3205793139297204,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.25459891577231136 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.0001673094901620184,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001328854921881024 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 4967.560430281524,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4967.144124902717 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 4967.669270777774,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4966.870837502001 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 1.0453175319464991,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9624980899567691 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.0002104287500106483,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019377293385374036 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3207.0293334310663,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3206.8362807261947 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3207.1601304794563,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3206.9137498167565 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.23213598902583735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.23175658631947116 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00007238349415952637,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007226954107772205 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3298.204206015625,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.9374548390515 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3298.0271578292663,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.7466777252303 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.47619288439617274,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.45015652371549253 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.0001443794424637626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013649637989798127 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3298.8635709024607,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3298.5514832561553 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3298.73908229565,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3298.294619005221 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.44135483689408955,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5059708034883286 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.000133789963545946,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015339181639477127 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3298.3082944677103,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3298.018902027288 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3298.1309417250773,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.924869270222 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.37569542830709973,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.29125307156396335 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00011390549177505873,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008831152283115495 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3331.1389599638965,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3330.882147361747 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3302.468511005227,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3302.1381227705606 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 52.37032304689016,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 52.41379460421471 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.01572144653114614,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01573570972654481 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3309.040538355111,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3308.6376434088 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3298.8719648045867,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3298.423521321138 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 17.85190281372009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.704751822082702 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.005394887915937737,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005351070056690153 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3298.5056451523983,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3298.216227634131 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3298.7145825382736,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3298.3599956654193 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.6022659234821001,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.42656890237584677 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.0001825875072753662,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012933321314771175 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3299.7260354667674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3299.4395088258607 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3299.9476246354716,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3299.604102752632 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 0.4529862966401583,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.44394636450651587 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.00013727997166167175,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013455205446833568 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1455.9648644369483,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1455.847289641483 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1456.0117208821848,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1455.9654686061185 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.2596138889893609,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.27242462415461616 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.00017831054535080343,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00018712445054707861 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1509.2233498161397,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1509.127215010707 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1509.3086019512084,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1509.1951056005657 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.1564578951481471,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16364056739017593 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.00010366782038404552,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010843391184156396 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1097.7025321451104,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1097.6189012613306 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1097.8869402405574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1097.777924658171 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.3298668713319323,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2955411613752672 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0003005066141983953,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00026925662544226016 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1100.8778087329217,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1100.7970494097597 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1100.7762300206866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1100.6626827451385 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.3850246243044219,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.36021785736761935 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.00034974328781100063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003272336690589477 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 2960.9669796306007,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2960.663337023529 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 2958.627823647212,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2958.304079235375 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 5.4396198864816165,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.453111414054156 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.0018371092700129484,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0018418546093580442 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3028.0186896858318,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3027.7771122752765 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3004.5508706440382,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3004.311154001174 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 72.9897363642884,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 72.93820355812986 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.02410478396745344,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.024089687203996055 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1278.7987323399982,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1278.6951260049807 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1278.6523456762511,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1278.5449957809515 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 0.38965353439936345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3758323392944285 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.0003047027843751139,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002939186453839385 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 390.73052089258385,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 390.6967656559585 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 390.13264304754085,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 390.11912671595377 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 1.1160399459814183,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1149916202061705 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.0028562906819562996,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0028538542374011225 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 961.8004815003629,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 961.7491597297734 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 961.7295978639972,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 961.6810243224468 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.5558156446577792,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5868035277326004 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.0005778907947631022,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006101419707998258 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1382.461629477388,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1382.3571245989617 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1382.6490641738058,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1382.5521513133244 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 1.0033190886058139,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0494288112916361 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.0007257482357648497,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007591589702958184 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 116.60295620495988,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 116.5914524348979 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 115.81777959538466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 115.80610246636911 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 1.4921224323248845,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.4865070854118205 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.0127966089444773,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.012749708956939646 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1809.3846483815796,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1809.250241613303 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1809.436575861475,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1809.3033086810062 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 0.7220851036511767,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7801146143679084 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.00039907772197418184,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00043118115804272744 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1493.2191003206506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1493.1307550801694 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1493.250403718712,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1493.154635458555 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 0.23319301794794334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2530836543525249 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.00015616798492456196,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00016949865475039143 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 961.8956868541278,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 961.8400395057138 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 956.5783277486344,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 956.5433646457424 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 12.136806043022666,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.132610620965531 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.012617590669021492,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.012613958790071204 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6083.949482387139,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6083.4991694424025 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6083.631806440695,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6082.609550976984 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 6.187658991290186,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.220576295367903 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.0010170464119078047,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0010225326119240787 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 5579.49740483976,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5578.970095890733 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 5578.643055223706,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5577.694429835017 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 15.059365458685882,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.00929229777748 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0026990541201118057,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0026903338859680897 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 647.1522816552335,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 647.1093644763688 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 647.1494199964869,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 647.1290408451952 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.3335884052615645,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.33864498676618476 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0005154712649831647,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005233195582638665 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6083.025657719754,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6082.715175295452 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6084.328388138852,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6083.846531129534 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 2.2569140241250305,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.291642590796275 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.000371018330534388,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00037674665420857296 ns\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "81ce1dca1dae522ea5c7eed474b55a0d0936f9ab",
          "message": "Bench: cap iterations on message-queueing benches so they don't OOM CI\n\nTier-2 benches (BM_Reverb_*, BM_Patcher_*) and BM_Engine_ListenerPosUpdate\ncall public-API setters that enqueue lock-free messages to the audio\nthread on every iteration.  Under engineInitOffline() there is no audio\nthread to drain those queues, and google-benchmark's default min-time\nloop will iterate 10^8+ times to fill its target sample window — the\nqueue grows unbounded and OOMs the 7 GB GHA runner mid-suite (most\nrecent run died after the first ~2 min, at BM_Reverb_SetDryWetBalance).\n\nCap the affected benches at 100k iterations via\n`BENCHMARK(...)->Iterations(BenchHelpers::kLeakyBenchIterations)`.\nMemory ceiling: 100k iters × 3 reps × ~60 B/msg × ~10 leaky benches\n≈ 200 MB peak, well under the runner budget.  Timing noise at 100k\niters of a 5 ns setter is ~1% (sub-millisecond OS jitter spread over\n~1 ms of work), still tight enough to catch the kind of 10%+\nregression we'd ever care about flagging.\n\nAn earlier attempt added a periodic state.PauseTiming() +\nrenderOffline(1) drain inside the inner loop.  That hung the full\nsuite locally at non-deterministic benches — the drain didn't actually\nshrink the relevant queues, and PauseTiming hid the wallclock cost\nfrom google-benchmark so it sized the iter count enormous.  Reverted\nin favour of the iter cap.\n\nLocal full-suite wallclock: 3:21 (was 3:51 before, 3:51 includes the\nno-cap 10^8-iter reverb benches that completed only because Windows\nhas more RAM than a GHA runner).  272 result entries.\n\nCo-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>",
          "timestamp": "2026-05-16T21:00:04+02:00",
          "tree_id": "3add98339ed865cd3ba8e3f2d0b4229090d12e2e",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/81ce1dca1dae522ea5c7eed474b55a0d0936f9ab"
        },
        "date": 1778958318753,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.331867785308829,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.327611823757595 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.324000724461648,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.318362834387132 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.022209282646032585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02025992835415277 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.001959896026569049,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001788543663869314 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 45.41305643711041,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.40265331507822 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 45.418749049032364,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.40343593657631 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.015431152367953555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.011845729323001078 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.00033979550328930525,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00026090390006055245 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 90.08059159800122,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.06118943457228 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 90.03410449071448,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.01859963324414 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.0875736858978759,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0783578945248741 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.0009721704125644203,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008700517394543139 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.326768176629358,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.325506631305457 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.325822010902394,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.323810591233716 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.0036574704640894833,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004509195220213206 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.0003229050340798871,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003981451220688963 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 45.3619695367179,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.357019691310406 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 45.3653141050888,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.36144078074606 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.018982023627167086,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.015326787892212245 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.00041845677824465334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00033791435143937774 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 90.00498461374139,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 89.9953686681214 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 90.0050966728097,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 89.99910348457841 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.016927713692203195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01550773909357864 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.00018807529121691316,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017231707945735504 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.432504255575209,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.431014285298021 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.43214933569449,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.43047257636263 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.008705394287500027,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.008862131472750823 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.0007614599647539803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007752707897626231 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 45.58187786623295,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.57514016019232 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 45.38496372073431,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.376239636382245 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.35400570310695456,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.35304069826668544 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.0077663694362447916,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007746343665116127 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 90.05504109420106,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.04760047762494 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 90.04322565059647,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.03220417535442 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.06407796428183765,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06831655978288893 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.0007115422246580246,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000758671629455182 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 96.50277441105705,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.4957347393634 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 96.50283560166672,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.49073388112258 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.007508877626551187,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.012236769252806265 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.00007780996631834494,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001268115040095605 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 351.47202315009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 351.44050251545536 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 351.4738324828138,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 351.438597569572 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.05154443776307511,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.026725331131059866 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00014665303172953814,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007604510846010005 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 681.6188609999992,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 681.5580893333325 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 681.6644359999913,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 681.5648569999979 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.08118805492359256,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03321469456839791 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.00011911063435727412,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000048733475676133386 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.12051385150883,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.118998248177425 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.101049587279457,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.099386168493014 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.06024532733398418,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06062893798098081 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.003984343913548446,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00401011607950213 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 53.44438058209142,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44069515349551 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 53.44933369583496,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44715301808552 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.023745505714768897,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02690271852559882 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.0004443031326426437,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005034125856396 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 105.1805469685288,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.17302971166384 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 105.22077776585002,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.20572763403271 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.07400168862266372,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06927957389677981 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0007035682049153619,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006587199597340935 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.072364913940469,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.071082909945032 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.047721841449771,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.045982036780378 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.05117312018961733,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05082331263437473 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.003395161972378149,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003372240265551037 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 53.44681118483879,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44135587298542 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 53.45299816140724,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44688028593544 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.025487886303242738,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02735110824460314 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.00047688319916966085,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005117966750246528 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 105.14187906580219,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.1309894710236 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 105.12662482656522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.1220470236553 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.03179337291861814,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03054875260358612 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0003023854357664705,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00029057799947755674 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.088940054046576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.087771346806774 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.088310542897284,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.08697767922511 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.03512265139913187,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03413291101642841 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.00232770832631896,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002262289786334309 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 53.453212437884225,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44797229927673 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 53.45682912011724,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.453329252697614 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.006732015451942148,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009877971484536787 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.00012594220524660038,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00018481470969985625 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 105.36684224655028,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.3583305429778 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 105.38445973912917,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.37237990141233 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.07274842821283814,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06826431819405944 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.0006904299935515998,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006479252076437662 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 134.96961574558304,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.95526598434284 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 134.98988504397866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.98458313889518 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.04638708775105569,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05140625090607451 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.00034368541019258057,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003809132643407818 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 539.7627739167744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.7231821434851 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 539.7988582706098,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.7349669083507 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.06718318242215757,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07680309623003606 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.0001244679805067781,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00014230090307593642 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1079.7498685352914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1079.670636052736 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1079.7329053469368,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1079.5658919629907 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.28207659172663535,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2776461040210659 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.00026124253398546784,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002571581505968681 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 2161.878108845801,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2161.6786886866166 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 2161.9706177590842,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2161.776808630889 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.1817490961897793,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.17295588923642824 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.0000840700016555572,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008000998952416561 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 5576.4677643772075,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5575.915682869436 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 5576.62461165896,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5576.006269217903 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 0.2904509369162858,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.46767711558270375 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.00005208510999950594,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008387449563118776 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3614.891065776892,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3614.4964334507554 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3601.7181121660396,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.3717020575746 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 23.19017220088813,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 23.15572175606705 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.006415178709100112,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006406347933219651 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3627.9400292138744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3627.679706626628 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3603.170973316574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.080708539919 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 44.10380478530515,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 44.083205192542515 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.012156707230593845,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01215190114827844 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3602.89061421852,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.549346559514 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3603.046542394621,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.3781601979686 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.3157202627787007,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.43276104791290365 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.00008762971085848011,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012012633451536106 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3608.6968178513216,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3608.4075143021996 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3603.511560069177,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.3954191875728 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 10.237995990894555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.9953351240883 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.002837034117205343,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0027700128337697512 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3602.5671669436438,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.187673326191 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3602.045616616608,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.6276838221997 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 1.14867796980302,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0768185092883635 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.0003188498413972775,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00029893459390305726 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3601.7239204144316,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.3718586587215 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3601.6698343769403,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.432353864295 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 0.3426129885288314,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1596145405269022 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.00009512472252159978,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00004432048308011945 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3601.6891720576073,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.3428027865816 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3601.6730980038087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.135691089952 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.4862447305230632,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4122399466070758 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.00013500463457408144,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00011446839947813361 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3602.3635934900963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.9477268439628 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3602.214050522176,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.6455162833604 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 0.39690474575093065,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5495702698382657 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.00011017897984206401,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015257585937256254 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1673.326193165825,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1673.125395957288 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1673.561401539466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1673.316872496799 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 1.1052112376151884,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0215704993305919 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.0006604876216777555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006105761718750881 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1678.163995659721,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1678.0068999009436 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1678.1063498322083,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1677.9204035119844 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.732507270218579,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7956666304315042 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.00043649325817564995,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004741736344936806 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1272.0829006161498,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1271.9273041205502 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1272.2120847095907,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.0155451146436 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.6046556121480074,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4858988114132356 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0004753272069415717,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003820177535611606 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1272.4875343254232,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.3541811047146 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1272.5728086466556,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.4783388478413 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.28023677556796284,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2501013951498226 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.0002202275212986846,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001965658610346008 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 3308.3919562349333,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3308.0581532249525 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 3308.0820074423245,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3307.7760209558223 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 0.7120743343453202,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.48870427846501147 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00021523276073844824,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00014773146535788207 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3353.4894376052475,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3353.135235655505 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3351.0788064233698,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3350.6461728219206 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 30.17974597998708,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 30.379567638351286 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.008999505303806374,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009060048433272445 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1442.1643569566852,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1442.0091282321816 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1442.147807699156,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1442.0481823915218 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 0.24156112147152972,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12549423789458267 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.00016749902346864398,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0000870273533208706 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 442.30056609330126,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 442.24376358820655 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 442.7526108586255,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 442.74026047732815 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 0.8370028618134263,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8645947039080054 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.0018923847853200902,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0019550184199161914 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 1086.1324591546015,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1086.0448222437653 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 1086.2164749708143,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1086.1880561355074 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.4234140153227867,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4124410609521865 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.00038983644375415664,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003797643085301807 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1611.0469378209793,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1610.8873449287673 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1611.081542137378,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1610.9965669863739 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 0.9350031396526547,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8631614936471151 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.000580369893454062,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005358298309093016 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 183.88558297830346,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 183.86839077244562 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 183.895905708403,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 183.87559512037535 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.07528962275697472,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06650109967884085 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.000409437333463266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00036167771632451064 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1834.797132925246,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1834.5727221381746 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1809.906385010361,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1809.8184448359732 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 43.119597439400046,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.12555710327829 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.02350101636067732,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.023507139609606712 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1631.723201487586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.5058930535106 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1630.637644144479,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1630.3690331232629 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 1.9467264245693918,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.0167159978191145 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.0011930494233302732,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0012361070875721134 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 1093.3924005140955,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1093.191495110464 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 1090.265768669845,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1090.1839365184658 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 5.497104935670656,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.419316109965652 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.005027568266512557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004957334679426905 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6851.064350782355,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6850.514714902856 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6851.535080850342,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6851.170754864675 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 0.9369718806877825,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3103585319520752 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.00013676296597342355,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019127884348623818 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 6131.1563875689535,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6130.598302716258 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 6129.4534793151,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6128.893857046529 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 6.838767511414917,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.336128963299707 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0011154123429767114,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0010335253837284338 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 730.3029203946809,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.2326772723459 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 730.3595451124803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.2799847395182 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.18278387451968678,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13239724883884615 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.00025028501107582054,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00018130830481784586 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6134.198261676425,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6133.607432952872 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6132.0784959037455,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6131.913941600768 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 4.624047273485106,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.374441616740637 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.000753814447500657,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007131923039676296 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 11.684603333321775,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.688306666712833 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 11.478099999919777,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.480600000055574 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.4048822669206822,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.40758999871364904 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.03465092099156263,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.034871603760570885 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 41.806203333483914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.812783333294114 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 42.62236000016628,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 42.62605999997505 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 7.158156596464778,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.158736721124087 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.1712223551936749,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.17120928458794624 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 11.39547333328513,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.401369999977833 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 12.349000000142496,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.35389000001419 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 1.923976322768082,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.9235932705708951 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.16883689395756157,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16871597628834384 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 16.223073333397526,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.99751999999436 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 18.16872000006242,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 18.174260000023423 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 5.377835569930631,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.771700572586172 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.3314930198126876,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.36078720780397255 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2672.8491966665993,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2672.665886666626 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2651.7230100000684,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2651.726809999957 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 38.02774066218303,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 37.87065356176641 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.014227417210671188,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.014169617590696699 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 550.3351733332806,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 550.3078899999991 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 550.5901600000129,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 550.4714600000682 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 1.6640868507470126,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.6555265493471136 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.0030237697522910254,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0030083641892670594 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 34.56344999998843,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.566026666690654 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 34.376700000109395,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.378980000013826 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 0.9371459808101767,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9373233221121313 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.02711378582897513,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.027116895185855347 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4870.022443930671,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4869.65246390292 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4863.141668922999,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4862.746613623217 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 18.25905944774486,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 18.52650422268579 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.0037492762421455473,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003804481810563788 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 16070.623550000011,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16068.888673333342 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 16210.971010000037,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16209.27120999994 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2683.617405154454,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2683.2515536196875 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.16698900305921557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16698426432392924 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 39.074710000003655,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 39.06993333326151 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 39.66662999999926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 39.65042999993784 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 7.307571946323683,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.307518130495002 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.18701538530479175,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18703687227113525 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 9.045307892424537,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.044327430353968 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 9.136527037582693,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.136058973134134 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.1593167642283271,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1598092270514257 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.017613194169073584,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.017669553461220854 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 9.138889048722115,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.137997166084572 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 9.140188316982004,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.1389375597474 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.0023548215293940685,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0021399108382434725 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.00025767043639985337,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00023417722717027024 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 9.001653333295204,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.033616666727086 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 8.392580000133876,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.394900000041616 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 1.1332859508747821,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1068959914464511 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.12589753336567605,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12253076838243611 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 126769059.42333339,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3960311.713333434 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 125728906.52000012,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3952639.489999967 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 2015879.321484363,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 303523.8182512873 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.01590198216074573,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07664139598643063 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 126461554.55666661,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4869469.360000001 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 126858493.08999991,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4872429.410000051 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 1897233.5574375195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 329311.8126584103 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.01500245322848203,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0676278642111448 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 753.6993551666682,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 34.83614723333327 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 744.7049021999987,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 35.51060869999958 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 47.79392695650094,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.67767730540123 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.06341245568126047,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.04815909446484168 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "92a518327d80383839cfd7f106fc9e0d507706d9",
          "message": "Fix issue #36 demos use hard-coded relative path for TestResources\n\nInject YSE_TEST_RESOURCES_DIR as a string-literal compile definition on\nyse_demo_common (PUBLIC), mirroring the existing YSE_TEST_FIXTURES_DIR\npattern in Tests/CMakeLists.txt. Demo source files concatenate the macro\nwith the file name, so the resolved path is absolute and demos no longer\nrequire launching from build/bin/.\n\nCo-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>",
          "timestamp": "2026-05-16T21:44:30+02:00",
          "tree_id": "6768923540e7f02fac80721edbf6409a937f0e9c",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/92a518327d80383839cfd7f106fc9e0d507706d9"
        },
        "date": 1778961049407,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.865740034966585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.863647166840908 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.86242786560394,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.860604660564922 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.028375973828755258,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.028891663558413504 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.002391420488324829,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0024353104194775937 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 41.75625657522643,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.74937809833022 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 41.757275146627585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.75051234602495 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.005080086298064634,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006330496822993986 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.00012166048191874072,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015163092509028718 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 86.950729691262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.94424598735054 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 86.93345380992189,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.92526418165694 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.030402109572734103,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.032910774474653695 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.00034964754960290254,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003785273435972044 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.850232455059967,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.849159916354006 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.848625500531815,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.84773021134131 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.002969099599846523,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0026037098834712524 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.00025055201331335386,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021973793094628226 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 41.9010594729048,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.89642315830566 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 41.75151730852168,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.74835199266958 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.2664938673586815,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.26663121349943597 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.006360074678565324,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006364056723696191 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 86.96328024909816,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.95527545248991 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 86.94053649478133,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.93765457914573 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.040352174855964755,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03749550565278221 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.0004640139463504565,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004312044951576143 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.853264422915274,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.851925461834568 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.85491679078541,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.853545270453713 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.0040732397681493655,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0036841465225442988 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.00034363864863039685,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003108479322121916 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 41.78934344570587,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.784381708426366 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 41.79095444794667,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.78445884727814 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.0045819298317439855,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006256452827706205 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.00010964349889097884,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00014973185127792639 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 88.37488858456915,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 88.36348790097232 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 87.24358298821215,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.23589188143903 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 2.095093001394219,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.0917905161801573 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.023706881388476642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.023672566190737022 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 80.89078773762635,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 80.88139536224514 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 80.79787517548299,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 80.78852150565179 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.16944379733870907,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16853302906939902 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.0020947230466874574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0020837057559972443 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 310.45796845639956,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 310.4286813880473 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 310.5011597355628,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 310.45330370878804 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.0824080009255986,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07779806766441294 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00026544012168646237,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002506149474222148 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 603.7664276666703,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 603.7108169999997 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 603.8462170000117,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 603.7935159999996 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.16292687183414903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16983236895506612 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.00026985083033483656,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002813141063117082 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.283407260205975,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.282239881253842 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.282647466742434,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.281713554853317 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.009959688800369158,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00954356963754004 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.0006516667802409221,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006244876216899843 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 60.61110344880455,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.60597353439227 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 60.77517485732463,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.770879350710025 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.3831591163491694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3817448723545174 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.006321599419037249,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006298799443224642 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 123.91295616052865,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.90250355836758 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 123.91478010492541,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.9014081983661 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.00474811807851522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006505548610395834 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0000383181728984341,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00005250538466586529 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.280914375897444,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.279573927124792 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.281183663488171,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.279304188344137 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.004139764531440967,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0041898769802143606 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0002709107864625307,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002742142549391614 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 61.02666415604599,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 61.02095326875079 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 60.802343448591664,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.79472329057003 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.6833600491871322,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6859202579147997 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.011197729035946838,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01124073324279029 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 124.07666136275726,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.06522143837564 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 123.98514415233228,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.98113548776182 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.20478810890620056,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.20494349270359422 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0016504966095716492,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016519012365233362 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.28623707966087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.284922162383749 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.284526832771514,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.283407515858789 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.0038688267463274566,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004134198763221295 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.0002530921590556208,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002704756176904567 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 60.54230229869656,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.53714784100998 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 60.71120909940814,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.7054946698913 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.32036605443883726,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3215278107040646 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.005291606732400967,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005311248087678331 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 123.91351240267676,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.90419639779765 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 123.91609616212808,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.9113563410342 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.0364198148314561,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.036331930355713506 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.0002939131828747142,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00029322598759342174 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 127.45117966353827,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 127.44087879270194 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 127.45406619469519,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 127.4416111625364 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.011221540481441847,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010193086020800676 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.00008804579534740979,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007998286042409491 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 537.4888392177753,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 537.0594649177916 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 536.8344808588432,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 536.7667373367415 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 1.932259413167718,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.314864008656061 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.0035949758807639564,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0024482652193036558 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1070.4437792203532,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1070.3554707207954 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1070.4349561675715,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1070.379441721367 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.8768828306164632,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8679292662940642 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.0008191769130137146,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008108794601755864 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 1915.6106404778668,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.4517093794873 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 1915.5429605279269,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.4294684231638 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.31440435248230586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.269852936986203 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00016412748281867696,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001408821405754062 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 4968.603562304838,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4968.202512063595 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 4968.321217712029,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4967.972899517455 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 1.1136221188525293,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9059036866011029 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.00022413181186384324,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001823403302102567 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3203.942202412818,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3203.70055874103 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3203.480091704898,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3203.198428569475 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 1.007920366841893,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.963582243647006 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00031458756218600024,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00030077163142415176 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3296.0105497652585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.7778222584416 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3296.124911104173,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.878964992697 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.3303285823766526,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.23530938359957462 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.00010022072969401707,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007139722283777252 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3296.126612931072,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.8778378327625 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3295.978577952759,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.736334617097 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.3582761858369156,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3408564316863873 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.00010869612363534769,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010341901261441196 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3296.5389452265354,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.2959463554125 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3296.6704221636046,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.3666979101367 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.34658713450192175,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.22691521005501294 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00010513667220700908,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00006883945305514949 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3297.9059249037896,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.599441464439 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3297.4264979442414,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.116491869174 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 1.2355939961158942,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.144484960673982 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.00037466017049953904,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000347066094894086 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3296.55941237407,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.3233794770367 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3296.771353234774,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.44978811564 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 1.0446065596323846,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9389958331490824 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.0003168778198600991,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002848615639458452 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3296.623197355206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.2468833768685 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3296.7236897926923,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.282951984988 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.22729391867004992,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.22366387225013637 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.00006894749720028721,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00006785410200251809 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3296.053636743221,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.7057545531734 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3296.1634014939045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.7818895931964 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 0.28069105781911574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.21128877220645312 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.00008515973608259063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00006411032657103797 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1434.9489569790412,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1434.7531487489096 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1434.9321855249555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1434.7756031940673 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.819232064579877,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8628369184996726 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.0005709137322240254,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006013835336427439 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1478.2429517055825,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1478.0624646396436 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1478.2929286310837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1478.095361494833 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.44610429728727924,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.33945342052682376 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.00030178009424808576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00022966107904619817 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1098.29277889384,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1098.195685018938 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1098.343218343891,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1098.2050834955025 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.09018224342898036,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07363769518752537 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.00008211129596955793,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000067053345949229 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1087.2856853798662,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1087.1708162106754 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1087.2510560805729,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1087.159710740289 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.42856643697676006,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.43934630025655347 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.0003941617577969228,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00040411892382090543 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 2921.516122520318,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2921.228693227309 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 2921.5006828803394,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2921.3283965467963 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 0.3759473615333966,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5100391711509145 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00012868228199578658,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017459748096183267 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 2995.1055475037642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2994.8095376534 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 2994.7842249499195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2994.5209982000965 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 2.4906222571084107,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.505659232152239 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.0008315641027022206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008366673074360391 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1286.9493188564916,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1286.8637451237112 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1278.2824866288506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1278.2491047836656 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 15.044751422440827,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.014712356652709 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.011690243898499996,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.011667678426366178 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 392.29654541825784,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 392.25914376442734 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 392.0461189099715,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 391.99298018801846 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 0.8763258884674839,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8763610514839683 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.0022338353439568647,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0022341379810135666 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 962.6444761009903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 962.5700618538925 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 962.3388066795688,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 962.2571131063565 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.8955670632310646,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9299056487075217 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.0009303196408069465,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009660654175307927 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1382.9606780191855,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1382.8221383732143 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1383.1429397451868,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1383.0123872429176 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 1.3626120655015856,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3621925788758915 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.0009852861958832078,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009850815524825255 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 171.2794546157189,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 171.26557086867592 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 169.47264946772702,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 169.46624176999785 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 3.7858775544053445,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.780913125435285 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.022103512431770098,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.022076317535731898 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1809.854511104655,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1809.6943198726797 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1809.8196892824135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1809.6687169339423 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 0.12123504642211724,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1840369580635157 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.00006698607301209019,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010169505205523524 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1488.3642980324946,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1488.2177291468408 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1485.3743182513338,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1485.2170466902082 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 5.823280457409056,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.802833766688928 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.003912537048293212,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0038991833338899626 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 979.701001197698,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 979.608241851245 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 970.0886355232096,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 970.0347107620605 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 19.997258384385553,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 19.967205359424383 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.02041159329217652,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.020382847455111996 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6084.018193699838,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6079.030231266981 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6075.565780114194,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6074.854976665007 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 15.591313649457064,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.164235781746857 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.0025626671638822973,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0013430161507924068 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 5446.833638398953,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5446.380995961695 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 5437.3538244373185,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5436.937789876938 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 18.287544042880903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 18.070552214134132 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0033574633001378688,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0033179008643598067 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 648.1600816885979,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 648.1011706250603 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 647.7647623993045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 647.6868894991827 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 1.137715315062848,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.150543146038572 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0017552998822433683,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0017752523806259016 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6071.719469861607,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6071.260954376113 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6070.700182657271,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6070.463455595243 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 2.8708876466518167,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.783111055533754 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.00047282942845138626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00045840741757735044 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 7.340880000015204,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.341499999995449 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 7.41427999997768,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.413030000122944 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.16984381061698287,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16961201816202243 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.02313670985176588,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.023103183022832877 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 40.099906666644834,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 40.10171333334256 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 42.602959999840095,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 42.60537999996927 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 5.180327575730894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.180186585423635 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.12918552700871744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12917619111093115 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 9.647196666643746,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.648900000058802 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 6.2149300001124175,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.216090000066288 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 6.015205510440006,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.015772742788051 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.6235184912564545,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6234672079461274 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 15.470783333266048,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.472200000061775 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 13.923559999966528,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.92457999997987 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 4.067250013294578,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.067613398559293 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.2628987767250915,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.26289819150108273 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2840.7820900000047,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2840.489480000059 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2836.9016099998134,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2836.8579700000396 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 8.0502469735613,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.13907373963663 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.0028338136184043903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0028653771812724555 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 601.5930166666786,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 601.5551633333397 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 599.8454199999514,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 599.8350200000857 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 7.0909121175617535,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.0464227668957085 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.011786892336036834,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.011713676810369384 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 34.91031666660168,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.90990000003081 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 33.98229999987735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 33.981500000095366 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 1.6965614389550454,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.6975685635969782 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.04859770981619675,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0486271391094068 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 5117.167133020791,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5116.613724484831 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 5107.716818048629,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5107.332215060138 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 18.9602195367826,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 19.014417354170227 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.0037052179543703716,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00371621122446266 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15366.988423333225,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15365.542183333315 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15634.064950000095,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15632.362730000013 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 1770.9450525715495,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1770.791418250198 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.11524346890784061,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1152443172601445 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 37.52869000000676,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 37.52324666663753 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 37.63719000005494,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 37.63571999996884 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 2.3682847810422056,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.3752280566898674 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.06310598054559802,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.063300174363689 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 8.093638084993938,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.0927438256259 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 8.0942586811445,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.092846513339031 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.002907832583026272,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0024493609332857788 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.000359273858367544,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00030266136999540353 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 8.093213629972555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.09240196406059 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 8.09382591832701,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.09307560916623 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.0014432214201701248,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016770990233037537 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.00017832488874694623,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002072436627285655 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 8.250380000068466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.250956666794687 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 7.606929999894874,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.607690000099865 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 1.1558508018792006,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1563306738283428 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.14009667456160915,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1401450426326807 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 134202486.7466665,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3431729.1933335047 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 134003813.80999987,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3447661.2200001 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 586657.8261870343,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 401845.472133344 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.004371437820630446,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11709708123647143 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 135071279.0033331,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4634393.079999862 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 135032282.23000008,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4673942.089999911 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 121794.05690740231,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 374025.0377688283 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.0009017021072584709,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08070636894892812 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 851.731738699999,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 35.27836970000067 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 851.0889512999996,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 35.458443999999645 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 3.5482190811548278,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.7761346423326092 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.0041658880606826815,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.050346278964602365 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "de63ca9af530332d0724a4d84bd993a538cc4412",
          "message": "Release v2.0.2",
          "timestamp": "2026-05-16T22:41:58+02:00",
          "tree_id": "fba77c0c8050e9e3431666b4bb87455852f2565b",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/de63ca9af530332d0724a4d84bd993a538cc4412"
        },
        "date": 1778964444650,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 12.025024755298388,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.022921607063857 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.852114974400772,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.849314554047893 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.30714268576381726,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.30651941360863505 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.025541958708108773,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.025494586393089755 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 41.836277802670345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.82885480619125 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 41.82121250503692,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.818272166199705 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.061126532634170234,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06027628525583718 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.0014610891753440989,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00144102164726048 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 86.95121307565479,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.94318466809233 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 86.94040375300199,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.9378474593467 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.026371141533120705,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.019203609394763625 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.0003032866431682283,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00022087538509284955 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.846337298216165,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.845619250919446 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.844710356463212,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.84396087220266 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.012164527017559676,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.011918680912538006 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.0010268597551575222,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001006167821206383 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 41.78320159278823,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.78137832115234 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 41.765291366621135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.76398799199706 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.03130468280928434,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03125078118848152 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.0007492169488201095,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007479595562471051 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 87.0348595325263,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.02683472301756 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 87.02452041677945,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.01979614377173 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.02944252460244165,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.025441061473773785 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.00033828427782362896,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000292335824401125 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 12.009138224919687,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.007935515865498 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.856007555748059,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.854564115410732 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.26741652238194813,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2674678566334509 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.022267752887300662,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02227425824198162 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 92.43819615384348,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.43203551916885 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 92.47256041935195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.4667302621073 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.07792241986033971,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07771451303659167 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.0008429677676818208,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008407746578346745 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 224.26714526760176,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 224.2511340792505 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 223.83194699710612,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 223.82452054760822 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.7697896627656364,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7615577631710256 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.003432467389938469,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0033960040661461743 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 81.24371834492621,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 81.23849257692798 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 80.83609293694552,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 80.83028253160718 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.7635583130882885,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7647577446841781 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.009398367389421361,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009413736277294886 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 310.57725666869106,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 310.5550280192886 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 310.5832921337841,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 310.5651822902424 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.024722339542938867,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.020502711054824794 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00007960125544321964,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00006601957529263178 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 604.494843666662,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 604.4538223333344 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 603.3246009999971,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 603.2586660000021 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 2.1465923189504834,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.160996552284691 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.003551051496039202,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003575122651955006 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.499385342951813,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.498288262647618 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.516314074850854,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.514671872718205 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.06894603353826495,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06843199258622344 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.004448307594960045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004415454882920922 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 60.210434048925585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.20553350807097 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 60.135527444976255,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.13236129288496 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.1298371956035945,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12953994032383753 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.0021563902943814,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0021516284762508053 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 124.05078388508264,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.03877461957022 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 124.0014765185332,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.9895607020657 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.08636874911109826,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08756009289825077 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0006962370281441187,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007059090447063798 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.546654089252513,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.545216251280747 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.518722621236002,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.517066891937048 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.08636859810774195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08646821881634546 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0055554460536591625,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005562368346546578 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 60.22084749867753,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.21563675311345 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 60.16094062516607,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.15807080688276 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.12627159765042503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12275156446566965 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0020968087115213383,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002038533030364788 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 124.87188282467149,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.8594385950264 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 125.3803343974691,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 125.36921175616851 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.9075593209696788,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9097263150105174 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.007267923734632504,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007286003567268603 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.572503258868318,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.571292231230743 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.506674684643386,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.505357406738574 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.14969003532718642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1497780964190881 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.009612458115360488,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00961886105500505 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 60.415909130826805,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.407723947307254 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 60.35832016106995,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.35372986135164 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.2942916853970851,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2939065918119359 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.004871095869133,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004865380991151168 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 123.9011858164583,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.88920162797405 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 123.89805108994636,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.89017565820058 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.006387406723592326,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0020346091118193456 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.000051552426084560196,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00001642281236042717 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 127.22328126123212,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 127.20744775001485 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 127.13995848547984,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 127.12998599472728 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.27501780858616404,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.27070432678172446 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.0021616940379132346,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0021280540689229644 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 537.427374134496,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 537.3570447542223 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 537.3752578078914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 537.3052890517539 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.30751550778468045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.32487749953464945 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.000572199189294965,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006045840520863417 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1070.123554351977,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1069.9927001238389 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1070.5748782377593,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1070.4260142523347 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.9067208915103439,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8953373636197237 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.0008473048629038138,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008367695999384848 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 1916.1346704400603,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.819896578598 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 1915.8706283561924,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.5698067143733 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.5369940747552335,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5595043173561326 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00028024860832558666,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00029204431917391276 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 4968.824181127776,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4968.214705986716 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 4969.6579864438,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4968.842137762155 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 1.557300527518928,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3109956862474819 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.0003134142949621266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00026387661641670345 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3204.7081593661046,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3204.3765458662947 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3204.06931676677,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3203.7471707442123 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 1.7055939294845839,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.5605975796526017 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.0005322150550588521,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004870206598115947 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3296.7203164695325,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.257286816947 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3296.643444571501,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.0806441742156 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 1.1614067326998183,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0938956452669022 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.0003522915568232286,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003318599096138002 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3295.764310414397,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.444495197557 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3295.8160503303206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.4023366312686 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.38570231809984534,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2841356978580001 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.0001170297029071683,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008622075057615758 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3296.389593613865,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.0174266395666 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3295.8558970882727,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.4385806306013 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 1.4467450985451935,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.364922331239024 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00043888777629561443,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00041411259546361744 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3296.1214783840855,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.6947689612894 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3296.3263878097746,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.84398706013 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 0.7113130484460668,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6514559316863334 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.00021580304400515787,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001976687701245022 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3298.358449907337,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.98672680954 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3298.4055456416486,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.895749659768 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 2.2130317842497473,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.2607161426690703 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.0006709494489029593,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006854836995830117 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3296.6613339738087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.2116060243948 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3296.577743974216,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.0821284932895 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.2695273424858154,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2880546432808194 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.00008175766788908403,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008738960895421577 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3296.5637364293125,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.2753280666934 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3296.4825099101963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.207757784648 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 0.45679437835504927,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.32352634023091187 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.0001385668274230997,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009814906463552728 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1436.7811706710966,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1436.5734089838327 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1435.2491304971418,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1434.9999979516797 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 3.1253863097943277,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.0485133628904184 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.002175269535537212,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002122072804512509 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1482.723078963959,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1482.5669863836345 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1482.7090053493293,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1482.5430220767842 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.16243501634599186,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1369299231097252 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.00010955182302786577,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009236002444903542 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1100.0654434634891,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1099.9468891053323 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1100.47172704134,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1100.3740331144716 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.8262809833047887,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8254888867822762 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.000751119843109782,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000750480677711364 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1087.893247535421,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1087.7678160454927 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1087.8496432774332,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1087.7252715646591 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.17464845040619192,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.17366902839675571 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.00016053822450121008,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015965634010768753 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 2920.1105139140614,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2919.8113779951545 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 2920.494779625993,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2920.1921272919953 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 1.3894868679842263,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2728762241020664 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00047583365813158346,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00043594467563725574 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3003.679385079004,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3003.352400230448 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3005.9992109066898,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3005.6352072871105 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 4.554952905228421,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.538699508624896 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.0015164577577272333,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0015112111080526683 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1279.5259887820312,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1279.3552612445849 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1279.0659991129457,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1278.8093182519458 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 1.156830903660808,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1881098182068668 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.0009041089542557745,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009286785728704061 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 393.5618269621137,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 393.16916536012445 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 393.7569044911451,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 393.70070575754045 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 1.9172329697369974,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.4584867422270966 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.004871491182303005,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003709565425587715 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 962.0647746167365,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 961.9489352918927 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 962.0609240083968,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 961.9400447211842 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.07126936400946426,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06085096694832124 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.00007407958995053765,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0000632580012470794 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1384.5080193594983,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1384.3699654783213 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1384.5797711082723,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1384.4758670172896 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 0.6456896553449893,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6103354915852833 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.00046636758062528134,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00044087599905015496 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 171.12437270045083,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 171.10227078530443 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 169.2557239145871,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 169.2271469843959 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 3.3177240080183807,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.3206462673670494 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.01938779354257139,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.019407376957221844 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1811.1031328412198,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1810.9092678804566 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1811.1336499181382,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1810.931588660993 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 0.7746575063719403,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6963845593070827 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.00042772688773205,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00038454966886449947 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1489.5378007419386,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1489.465258710679 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1487.1341155468053,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1487.0668711955661 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 5.953908800739551,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.950303508659949 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.0039971518666890565,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003994926013790137 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 966.9864127605434,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 966.9181973734009 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 966.8423600111718,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 966.7791055420615 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 0.4857108526934861,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.49449154270995277 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.0005022933582974382,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005114099042227374 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6077.759748285066,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6077.251514331936 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6077.633742486466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6076.956406431223 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 1.6751668636847696,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.6277505424587895 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.0002756224222514626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002678432081706801 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 5440.609768159679,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5440.208094476132 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 5437.883973651882,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5437.339301527073 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 5.247336253552673,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.289691722988453 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0009644757623055215,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009723326077102621 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 647.9964153498132,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 647.9442750674463 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 647.8720693104432,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 647.8453596144096 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.33325439973223636,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.30586839046504155 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0005142843260210519,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004720597159890068 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6070.916413108152,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6070.598879096325 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6071.253689714481,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6070.838754240569 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 0.7732638127697967,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9669146534793421 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.0001273718430878388,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001592782973701069 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 7.353729999882337,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.353173333370933 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 7.323939999821505,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.324590000052923 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.12119280216529375,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11755089291688675 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.016480453071738135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.015986416692151822 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 43.42578666656512,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.436913333323446 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 43.32038999990573,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.332249999963324 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 7.7456628256622295,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.745405384876964 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.17836551552043292,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.17831389918157306 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 9.536226666663575,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.538219999948675 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 10.488530000145602,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.491099999967446 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 2.9650115866733944,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.9656932183810345 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.310920838012207,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.31092732379804544 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 16.02137666679937,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.02446999996232 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 14.592880000066089,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 14.595269999944096 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 4.355414658379313,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.356330763370551 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.27185021293487915,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.27185490461655165 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2864.2790566666085,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2863.8782300000307 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2861.1967199998394,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2860.6378400000667 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 5.855639235440397,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.955422393404547 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.0020443675771784175,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0020794956751371664 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 578.2835799999475,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 578.2187533332698 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 576.4950199997543,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 576.5181299999257 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 3.7493207123405052,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.6097415325771536 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.006483533065802846,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006242864853081989 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 34.917973333297894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.91970000003637 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 34.06646000001956,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.06753999996681 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 1.5604955256715485,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.5619675592338602 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.04469032354129942,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04473026856565873 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 5107.956383673592,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5107.639471349806 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 5108.83326753241,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5108.372448382828 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 16.406972573541033,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.480158901842252 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.0032120424179779895,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003226570511541412 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15946.405613333356,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15941.600936666682 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15902.989370000001,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15901.266230000036 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2343.5570044784267,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2338.5978001977605 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.14696459260505051,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.14669780089770273 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 36.990719999986744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 36.984789999981636 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 41.83173000001262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.81400999982543 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 9.674782579869605,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.670421007332354 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.2615462088835544,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.261470215386843 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 8.098966625966234,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.098472987099674 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 8.097202568659835,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.096362251410492 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.006035287169710361,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0063751470521257285 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.0007451922508682188,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007872035953297505 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 8.097794840807301,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.09731059532094 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 8.098758532499225,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.098200566978985 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.002045823554865339,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0020111628972507138 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.0002526395883180195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00024837418221463205 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 7.997856666672003,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.9974733334135335 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 7.572600000003149,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.57285000020147 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 0.8246768699454595,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8249243674233736 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.1031122342292009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10314812354257316 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 133902323.18666677,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3857227.8466665465 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 133764800.33999996,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3813528.2799999 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 309627.7043796614,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 284541.597944249 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.0023123400476631343,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07376841847446283 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 134116673.76333316,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5001874.02000004 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 134288462.45999977,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4996265.940000057 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 1467480.2365695182,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 385193.0095309534 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.010941818011078054,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07700973834821821 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 841.7227063999991,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 37.14154173333289 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 842.0307868999998,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 36.87696689999882 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 3.2540528237941957,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.6551557244393235 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.003865943973059248,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.017639432663920634 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "c11cb1a0d59c9ed575171a17448062d2f9740e2e",
          "message": "Android: NDK + CMake + Oboe build with NativeActivity test APK\n\nCross-compile path from root CMakeLists.txt via the NDK toolchain, no\nexternalNativeBuild. Replaces the deprecated OpenSL ES backend with Oboe\n1.9.3, builds libsndfile 1.2.2 from source (WAV-only), and packages\nlibyse_tests.so into a thin Gradle/NativeActivity APK that streams doctest\noutput to logcat.\n\nStale ~2018 YSEAndroidStudioNative/ and Yse.Android.Native/ projects are\nretired (superseded by Tests/Android/). 16 KB page-size alignment via\n-Wl,-z,max-page-size=16384 so installs are clean on Android 15 QPR1+ /\nPixel 8/9. Tests build for arm64-v8a and x86_64; on-device run on a Pixel 7\nPro (Android 16) passes 767/772 cases (40,661/41,079 assertions). The 5\nfailing cases are pre-existing engine-level fragilities surfaced by the\nnew platform — see issue #48.\n\nCI: new build-android matrix job (build-only; emulator/device run deferred\nbecause GitHub-hosted runners lack KVM).\n\nCo-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>",
          "timestamp": "2026-05-17T00:40:53+02:00",
          "tree_id": "6d730c2c3f541bb20bffaff0522824c68129c601",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/c11cb1a0d59c9ed575171a17448062d2f9740e2e"
        },
        "date": 1778971574644,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.418742586891412,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.416313868125355 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.41574715621841,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.412734026490023 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.07733361453524863,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07722606450358438 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.0067725157955681355,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006764535855938717 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 45.45939622908737,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.4486769620662 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 45.3865369321114,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.378242480840555 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.13900227093048298,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1297132159437844 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.0030577236492538773,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002854059229315954 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 90.09832029941954,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.07536822716042 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 90.03345350373894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.00041509987119 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.11629818709683264,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13021501954723327 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.0012907919560580516,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014456229500926932 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.327623232161542,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.32539554609202 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.32934472491202,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.326100243522903 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.003523966328119458,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0029196355565864365 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.00031109494515267464,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002577954601853969 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 45.34869083332168,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.34386591004457 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 45.34789745199517,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.34127010616037 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.004460642592109376,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00552025504838105 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.00009836320542315962,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012174204685882779 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 91.85836075286494,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 91.84800247459356 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 92.69251219057408,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.67946459313646 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 1.6194398300470914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.6234354024806772 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.01762974885219235,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01767523907697113 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.423523844441759,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.422359606361729 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.423000891101028,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.42174505720091 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.007582837652218201,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007730460124306786 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.000663791467105635,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006767831158109639 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 45.687482967761575,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.68340752539485 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 45.74988033677898,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.74622620056704 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.11118541606674516,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11304739202739972 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.0024336078252592914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002474583183501758 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 90.11425862379593,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.10699107310336 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 90.11468797440237,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.11201468116496 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.030754581548685902,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.027625330994848377 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.000341284298604491,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00030658365866901584 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 96.53172638521153,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.52123819368178 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 96.5214477469965,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.51614318300273 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.022537015261952564,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.012441484934427096 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.00023346744232065442,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001288989363093511 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 351.5419380244182,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 351.5114269418418 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 351.5343939860896,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 351.5143206658526 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.01707996189020521,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.015273928975353604 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.000048585844369495476,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0000434521549078423 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 681.7354363333267,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 681.6626589999994 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 681.757546,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 681.6909169999975 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.10237308647995154,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0730011074934254 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.00015016541758568262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010709271885380692 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.114166028719104,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.112781248539548 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.104122161357749,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.102373907644086 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.026106973778478242,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.026561256612207134 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.0017273181814247114,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0017575359674298157 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 53.57989586040901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.57528062196106 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 53.533620299562905,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.5278127588932 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.10468495733676808,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10446225124986085 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.0019538103920452255,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0019498218215032678 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 105.62037211501932,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.61156391030744 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 105.58789498167305,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.5731784634458 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.07446116334783807,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07609046299633442 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0007049886480872342,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007204747300300907 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.131107640471242,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.129430417375241 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.147432243284777,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.145101247499255 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.03367429940935456,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03293859085169263 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0022255012791849924,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0021771203504042445 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 53.874745478323184,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.86881775580466 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 53.50492686696344,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.49922382207978 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.7111594114273146,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7087500666199689 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.013200237051949578,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01315696345579437 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 105.60865880766953,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.59873226193685 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 105.60087132554496,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.59290588547226 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.06533634897927276,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05925367195346163 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0006186646977333634,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005611210540528399 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.172374752839866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.171034914034024 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.20808499424063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.20622961955254 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.09253201247538988,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09241824105055353 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.006098716514899577,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006091755873889768 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 53.8789419115073,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.86927725093818 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 53.67674290572177,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.67262181641911 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.5076492066570328,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.49904741766568644 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.009422033704574489,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00926404516884427 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 105.66997389417872,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.65732540444746 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 105.69880173872234,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.68467571016521 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.05157481342954243,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05194895986662867 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.000488074440911579,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004916740005273876 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 134.9750112852827,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.95980036376343 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 134.9949747804098,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.97321932708618 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.051974013694785486,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05122239750267562 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.00038506396998873657,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000379538183700728 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 539.7480395894144,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.7005473150463 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 539.7278482477795,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.6941219320609 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.0376744384134497,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.046126601351711254 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.00006980004678128817,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008546702718977452 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1079.5687623006952,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1079.4635483245872 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1079.6252160732286,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1079.5353434889678 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.11230771391083284,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1420229006948698 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.0001040301626285399,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013156803758246452 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 2161.2324255128383,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2161.07679223032 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 2161.321375869802,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2161.220374406494 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.2970637163446038,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3130660873040699 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00013745107321074624,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00014486578562577262 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 5576.094415553837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5575.571842307164 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 5576.301093422865,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5575.824295805523 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 0.5026385152961566,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4452241069208996 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.00009014167943320824,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007985263566017767 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3601.6709709405873,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.3132959003906 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3601.418115006001,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.1075588127783 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.7861015258481522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5825352130123075 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00021826022759731978,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001617563275251407 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3602.015609593803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.764029857663 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3601.719814392702,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.505002855094 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.5612134118996542,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.452936040532476 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.0001558053803000987,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001257539463378936 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3622.0631207170973,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3621.728374976197 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3602.947392537766,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.5276346206333 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 33.613975425735454,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 33.57733088209949 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.009280339493112022,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00927107927642977 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3624.520038271025,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3624.103326594752 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3603.942475167649,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.383188530994 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 36.66714017055564,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 36.67013594197257 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.010116412596258308,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010118402439819018 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3616.0328218858745,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3615.660218652373 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3619.5168571722566,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3619.4119606631525 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 11.97813862864865,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.149534283736983 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.003312508270431482,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0033602533283023346 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3602.1850457017736,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.8559752933156 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3602.144885016855,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.836279456895 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 0.48492580254345535,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.30689932051619356 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.00013461990330621191,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0000852058834726731 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3606.1176592693314,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3605.833854852848 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3603.228554224392,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.880064229224 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 5.37964521054241,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.3689217115299 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.0014918107834652377,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014889542690116548 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3602.7244789977744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.393222641286 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3602.6132964244857,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.354828585114 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 0.4449938123470825,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4783454409634533 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.00012351591550816378,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001327854599428568 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1674.4181950735674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1674.2732503893942 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1674.5367020831134,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1674.439805906446 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.26947949886921785,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3369443363370301 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.00016093918452515262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002012481154188333 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1679.6010740930985,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1679.4448182482374 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1679.4646623537003,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1679.3414805507489 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.5367487922887103,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4923234157017924 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.0003195692123372379,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00029314652696676004 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1272.2973869037637,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.2102907626756 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1272.2621080446904,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.2012343390402 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.4605214697069122,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.45432337518478105 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0003619605561146578,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003571134257312282 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1272.98914199602,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.8909256984437 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1272.9323534115251,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.8808557265327 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.3415065281235458,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3547752462458423 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.0002682713597918603,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002787161406238911 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 3329.9542785360704,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3329.6362088365445 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 3311.830480870774,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3311.3877455366105 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 32.78072770708059,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.811682488609414 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.009844197536997955,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009854434668126884 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3320.310994843883,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3319.866354863811 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3320.3278103758753,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3320.048017949155 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 0.6746450194231309,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5357220103138937 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.00020318729795816968,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001613685471190813 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1443.8279280466722,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1443.655974853128 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1442.6941393072875,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1442.5106204203666 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 2.2314992406899266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.130516879430876 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.0015455437572182722,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001475778798094627 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 439.9747586045557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 439.9116278746895 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 439.92665937148786,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 439.8851165526283 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 0.7113388475268643,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7105551394192187 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.0016167719479703324,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016152224546827005 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 1085.9946155414852,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.8571739123709 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 1085.998545062673,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.7240059998755 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.23845233192428314,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2969268109317021 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.00021957045505735677,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00027344923261119814 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1631.5407201353344,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.316867812985 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1612.1923704869266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1611.961186935987 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 34.61377894018353,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.60502975970982 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.021215393837864104,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.021212941791071433 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 184.003332196382,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 183.98332666753925 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 184.0153898385963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 183.99077597728535 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.030998956356941932,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01860990624451329 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.0001684695379530276,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010114996060562422 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1815.5270679368884,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1815.264372930082 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1811.739682194718,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1811.382512177005 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 8.170438339407255,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.235127437972478 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.004500312049157109,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004536599495245903 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1630.9978793872258,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1630.8268163661132 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1631.2866666666132,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.235925538101 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 0.6721226798071792,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7379333209392177 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.00041209292072145375,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004524903034054322 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 1090.232658616979,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1090.0762755780897 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 1089.9669712149919,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1089.7476248500157 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 0.9766612609829779,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0255806330587127 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.0008958282924875202,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009408338260685715 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6851.889148076945,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6850.984340899912 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6852.456482534156,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6851.344405457363 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 1.2790435349804383,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.446992829567315 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.00018667020252938787,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002112094784582802 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 6135.369871113854,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6134.530254464761 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 6133.1689338124315,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6132.3676112862695 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 4.525136661045697,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.594767967369775 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0007375491219120544,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007490007835605124 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 730.6070957285342,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.5172657741797 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 730.6486002274988,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.6091029560657 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.1565401571388629,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16057992778279256 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.00021426038434894595,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021981674534771588 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6135.722210542955,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6135.036655512916 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6135.7900877700595,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6135.129561500274 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 2.7546916736193072,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.7344883875118478 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.0004489596463291551,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00044571671549095796 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 11.095503333346336,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.097393333301397 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 11.11764000029325,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.119819999976244 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.1572579129209913,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.15534887658119118 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.014173121146146623,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013998681664730719 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 44.09156333357108,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 44.10227666667765 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 43.848500000081,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.85818000002928 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 10.930722036717171,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.926280141166828 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.247909604701963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.24774866439995819 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 12.249306666755425,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.254450000028783 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 12.486200000125791,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.490170000063472 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 2.7141347455454152,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.713160677085995 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.22157456086160104,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.22140207655828065 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 19.83341999997871,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 19.838383333355598 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 20.388679999996384,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 20.391200000062778 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 1.442698613817355,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.442772938091781 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.0727407887201957,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07272633630715014 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2683.2684900000463,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2682.90580666663 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2684.0826400001565,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2684.0448099999035 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 3.5884449600200488,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.7649690145388846 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.0013373409978887306,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001403317628663476 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 550.6306466666425,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 550.5698333332987 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 551.2806600000886,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 551.2866999998778 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 1.6849559818611837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.7933849595396876 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.0030600475873644446,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003257325140177132 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 34.11741999987801,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.122843333316645 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 33.471579999968526,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 33.47605000001863 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 1.2184839679606803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2203999178691471 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.03571442295358315,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.035764895262335325 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4701.395523668692,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4700.749642834079 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4709.810339870228,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4708.9654579859 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 18.762105618202778,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 18.746017292727476 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.003990752431644369,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003987878257099758 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15752.927636666716,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15751.075786666554 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15812.421050000012,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15810.969839999983 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2409.4319196603724,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2409.04320589359 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.15295137356259722,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1529446774634193 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 29.221946666477077,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 29.21918666667504 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 29.29688999984137,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 29.285619999939172 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 2.464209859949641,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.465431381964101 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.08432736833294345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08437713924378211 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 8.86895078315731,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.867499561678295 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 8.854956591087932,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.852823624506295 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.08318490570597066,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08340059252159147 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.009379340097809991,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009405198381065022 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 9.140418655877653,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.139149011809623 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 9.14030189670151,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.138871590267014 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.0020662169734535704,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0012620443873394911 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.00022605277189627527,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013809211182667835 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 9.010463333349131,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.012359999947725 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 8.39884000015445,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.399679999797627 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 1.068130166611008,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0694393614431827 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.1185433120467503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11866363099669633 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 117893236.65000002,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3875106.6800000444 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 113336245.89000009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3835561.2899999875 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 8859468.843931379,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 288922.00445865095 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.07514823662220133,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07455846466106776 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 123386888.78666687,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4801133.47666664 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 123952125.56000046,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4820286.720000126 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 1964923.3652385187,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 295914.7797430693 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.01592489594770338,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06163435804924109 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 737.2047260666646,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 34.58165916666758 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 731.3708136000001,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 34.29661380000084 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 18.200371176436885,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.234326812765917 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.024688353903459703,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.035693105608873 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "35c13178d5d5ad1cf4234911e52debe03e2f3ed6",
          "message": "C API bridge: extern \"C\" yse_* surface for language bindings\n\nAdds YseEngine/c_api/ — a flat C ABI layer folded into libyse.dll so Dart\nFFI, Python ctypes, and other bindings can call the engine without C++ ABI\ncompatibility. Opaque handles (YseSystem*, YseListener*, YseChannel*,\nYseSound*) wrap the corresponding C++ classes; PODs (yse_pos_t) are\nlayout-compatible with YSE::Pos. Errors surface as a YseStatus return plus\na thread-local yse_last_error() string.\n\nM1 scope: system lifecycle, listener position/orientation, pre-built and\nuser-created channels, file-based sound creation + transport + properties.\n80 yse_* symbols exported from libyse.dll. Other subsystems (DSP, patcher,\nMIDI, music, player, reverb zones, devices) land in subsequent milestones.\n\nBuild is gated on YSE_BUILD_C_API (default ON); set to OFF to ship a\nC++-only DLL.",
          "timestamp": "2026-05-17T03:09:01+02:00",
          "tree_id": "8d140975aa53366d6d4382a5487657c64f44ee36",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/35c13178d5d5ad1cf4234911e52debe03e2f3ed6"
        },
        "date": 1778980493209,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.49748560748179,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.492813014329966 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.371211429844173,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.366986722608694 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.2561786850802123,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.255822042055411 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.022281279040132777,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.022259306032077254 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 45.393138725322395,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.38443765898162 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 45.390302661392276,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.383450624308246 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.006747847312246422,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003144988580458875 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.00014865346397565057,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00006929662991729235 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 90.05628667380586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.04383552057975 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 90.04955461881484,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.04243692791283 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.032988814495578195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03590795144229505 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.0003663132882112655,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003987830064623157 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.347195385531009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.346075988952022 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.343664918905189,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.342100675294212 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.007924427001838869,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.008478125363728873 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.0006983599676042798,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007472297358121215 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 45.40892705563876,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.40333943106015 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 45.39263232563841,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.386564850490146 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.03500442408793057,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03320786694059702 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.0007708709797313702,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007313970152133726 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 90.06783066940541,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.05735175416316 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 90.07525905980651,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.0622716207396 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.018702053277543245,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013327510210674244 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.0002076440959945983,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001479891419309711 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.443446909521448,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.442036698147078 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.447004354183619,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.446021157286177 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.009077614645681393,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.008625949011082913 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.0007932587722435642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007538823059779034 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 45.38176561430479,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.37769668202085 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 45.38178227219135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.37902309068519 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.008188013019374706,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00745025428251317 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.00018042517536589106,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00016418317427435765 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 90.01819818385609,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.00924647745161 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 90.01923014011163,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.00673770365553 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.0019081081439060522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004649165956061993 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.00002119691553933206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00005165209284611292 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 96.40829422479533,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.39768246405777 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 96.4141253046419,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.39856501035344 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.019360220017489816,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0209137202930882 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.000200814879810523,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021695252166343274 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 348.68412279128523,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 348.63748194120353 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 348.69091259247506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 348.62649803133604 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.033070775340780144,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04318175735148824 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00009484451163431837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012385861987946174 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 675.4692990000042,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 675.3913803333329 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 675.4258900000423,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 675.3516180000005 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.11380492332941894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11654393720801559 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.0001684827474733507,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017255763191780218 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 16.22720566775069,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.225773451668495 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 16.237610143354935,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.236838600334547 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.023043256678265844,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02368502802002445 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.001420038492767803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014597164252647026 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 53.48400458354852,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.47813551021835 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 53.510742826256866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.506195632584586 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.0513777204347503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05099704886783027 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.0009606184285339377,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009536055881769849 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 105.25766139942311,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.24702731708847 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 105.24621762812785,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.23454666202845 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.04351728027550744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.048596390608307005 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.00041343575087015894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00046173646750036646 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 16.232199643512384,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.230410748325756 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 16.217573060979486,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.215900016074904 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.028957482932320065,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.029265591753524885 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0017839531036013144,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0018031331558595195 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 53.46981132676003,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.464872838301055 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 53.46625948753299,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.46206586068386 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.011053235734089487,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010209760760293946 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.00020671918340130135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019096203204620546 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 105.3032945724708,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.28910525923341 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 105.29313347779532,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.277553369837 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.028300862751742345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.023451604325605904 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0002687557200051837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00022273533684102892 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 16.51885725889162,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.516938344435598 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 16.504150278244833,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.50208753803084 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.0397308309997615,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03904229838436193 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.002405180357035626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002363773331969536 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 53.477045212307594,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.4711001434741 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 53.4786763309334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.47582950412593 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.02843826614932217,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02782175225842862 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.0005317845448719216,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005203138178151763 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 105.36190700255673,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.34869783881771 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 105.36390098343605,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.34952887742493 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.006463957257059775,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0015356790505408919 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.00006135004045535089,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000014577105194887775 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 134.97647213244704,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.96146475097697 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 134.97235783457543,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.95532708272813 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.014568773210952402,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013028269738321942 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.00010793564967868357,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009653325682527931 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 540.0010912795425,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.9360404778566 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 540.0138688906629,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.9571678557618 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.05099140715697045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.039642708972484016 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.0000944283409430625,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007342112028194903 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1080.6658498043334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1080.5116598354746 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1079.9845850669283,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1079.7997077196576 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 1.256502851140067,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3072529836581885 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.001162711722006919,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0012098462536325053 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 2162.537678107076,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2162.2587151789244 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 2162.4915200326027,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2162.20138532899 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.3615452354872613,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.36112272250711536 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00016718563525965063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00016701180111892058 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 5578.3819479318545,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5577.625993320748 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 5578.540717183904,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5577.530522943039 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 1.1311690488454935,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8641383657927761 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.00020277726756678366,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001549294210166814 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3603.0805555555485,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.7186020176982 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3602.866486514535,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.7211962116526 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.5959966379131796,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5443324139760962 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00016541307603967366,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015108935060075007 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3603.0199993477604,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.5885966131696 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3602.8596510192588,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.4411269520006 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.37237197872110867,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3690294506350494 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.00010334996164010125,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010243452471425069 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3602.818318426592,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.361730085066 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3602.4311386490863,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.9500766784276 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.7257533429598345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8326448971404212 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.00020144044989667492,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00023113861392280537 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3602.926163616694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.619713719078 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3602.866253577084,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.461241945739 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.25210545499541986,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3304355434114019 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00006997241784781708,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009172090580448324 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3602.886214460768,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.407381490187 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3602.7742229265646,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.3943226431024 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 0.9763932795088214,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8993770012259027 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.0002710030851348868,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002496599928833875 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3602.9236548623307,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.4700432365457 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3602.8833590690683,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.505898702863 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 0.18383977785239328,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.20709209028513395 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.00005102516607708077,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00005748613806628005 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3602.766994005566,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.3415854323903 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3602.4783884214867,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.0559461434805 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.6682647478027848,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5650723930022871 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.00018548652991288956,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001568625238892944 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3603.1744237496737,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.7137631199785 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3603.31920148159,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.787975920965 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 0.4447203155557176,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5633458150128287 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.00012342458711530143,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015636707550281952 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1672.195411683906,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1671.9847857244558 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1672.0738359503491,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1671.9665593120333 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.2701507900800368,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.21342003822111622 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.00016155455767456872,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012764472502579816 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1683.148938182904,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1682.9378629304954 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1683.263892763506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1683.046259511145 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.5161794981432721,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4771075021923758 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.00030667487970525645,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002834968020516157 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1272.5245646014744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.3771691174004 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1272.4991194799702,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.3824837390036 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.27702581920441,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3150335419373432 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0002176978165377642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00024759446301277944 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1273.4454461644627,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1273.2883868570614 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1273.3775973146599,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1273.273586535334 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.1736434544679729,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18263284662958115 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.00013635719927459483,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001434340001171183 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 3310.5650612918907,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3310.1703713168445 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 3310.33244838172,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3310.181594900369 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 0.7603720942540179,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7321679142856891 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00022968045641045214,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002211873807553959 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3321.7885990668924,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3321.3587500810413 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3320.9499997630755,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3320.4896664911507 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 1.933358118522477,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.8339159329242307 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.0005820232266031522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005521583396793506 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1443.5805340561722,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1443.3001920348386 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1443.4992754893728,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1443.3477239232734 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 0.2307285305704169,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1902100979301337 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.00015983072999891176,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001317883133251481 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 745.5219779565168,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 745.4169479189513 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 734.5717603100237,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 734.450553538119 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 19.281244062303276,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 19.270040102850512 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.025862744000053978,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02585135762830245 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 1085.5698274707918,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.4268163163424 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 1085.5983521791097,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.412257567368 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.05128668192522163,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.028075139427394726 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.000047244019341170894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00002586552958279995 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1598.32079733381,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1598.0723059484826 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1590.5485708870553,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1590.1955358694056 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 21.69213403648429,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 21.7974636391273 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.013571827428304355,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013639848183333695 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 123.44005251489182,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.42382154312388 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 123.23943575788259,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.22437617312363 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.45118405546453516,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.447207393310254 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.003655086386244888,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0036233474844562415 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1823.9066432125055,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1823.6502444153227 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1812.9501564190239,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1812.8547097156113 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 20.119452527066873,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 20.057018005965237 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.01103096619661949,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010998281094407817 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1630.4851776720095,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1630.2985932350873 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1630.6167179629522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1630.3493681216094 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 0.5150172673864156,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5122910102926229 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.0003158674941907489,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003142314005657436 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 1024.76877982276,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1024.6524940635366 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 1024.7006359693598,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1024.5301931878655 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 0.40950100230928427,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3725395762085985 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.0003996033157646645,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003635765084913736 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 7218.583177808606,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7217.8244640813145 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 7217.591289195898,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7216.764319519853 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 5.375283631175239,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.788539388366168 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.0007446452439171103,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006634325082572721 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 6144.718505994561,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6143.930646319871 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 6136.503267515326,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6135.897270354092 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 14.522176834735264,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 14.29172677887545 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.002363359171061777,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0023261536631172743 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 730.0147688769176,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 729.9203389251394 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 729.9300281785872,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 729.7822846695053 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.2434429663232753,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.27495500118100724 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0003334767688300295,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00037669179295085603 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6137.475478940937,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6136.7272010235965 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6135.532601265925,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6135.15042153791 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 3.818229650101406,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.613821578772517 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.0006221172961427893,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005888841821369765 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 11.368486667227748,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.372093333363864 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 11.288100000683698,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.292469999943933 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.1844057746901371,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18651951491763874 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.016220784708463335,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016401511089468544 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 46.23523333331528,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 46.24657999997339 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 43.58815000045979,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.60058000003165 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 12.633889486865266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.629082520177795 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.2732524219308262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.27308143694485215 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 12.92142999962683,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.925669999977645 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 14.419919999681952,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 14.423329999999623 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 2.794035763872158,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.7935735536909263 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.21623270520003204,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2161260154170544 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 18.29021333340582,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 18.25362000005271 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 18.28973999977279,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 18.29208000003746 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 1.8433800461144259,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.90502119456276 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.10078504895006438,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1043640217423864 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2605.177769999803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2604.768980000026 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2606.1726599994017,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2605.6327500000975 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 3.878818847953154,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.773434652901735 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.0014888883563417812,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014486638476866754 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 543.769966666711,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 543.7709500000191 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 544.7941099998843,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 544.7918299999799 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 2.9168034737986517,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.9214680986220363 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.005364039304484845,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005372607894228138 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 32.373563333673395,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.376873333343305 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 31.721080000579605,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 31.723740000018097 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 1.222070917673062,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.222131847699753 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.03774903939604089,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.037747062080918756 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4710.380191061505,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4709.780072434468 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4709.0045361406865,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4708.542739478576 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 3.306677285996551,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.2728940531121964 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.0007019979602222674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006949144127276519 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15764.144486666963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15761.94307333329 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15666.23234000076,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15663.963540000055 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2464.398184554258,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2463.9944739799 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.15632933246955472,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.15632555342422144 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 35.4868466664963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 35.48017666673787 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 38.28575999932582,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 38.26665000019602 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 8.151022220451626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.147910922346307 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.22969136415682537,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.22964685319576925 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 7.385436541363994,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.384597844676752 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 7.384610509872213,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.383690065745125 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.00200671185073425,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0019031635928010847 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.0002717120158700377,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00025772068199665004 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 7.735341092606384,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.734372587369396 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 7.734230941743604,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.733344889748637 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.003252885595964573,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0027304093665512287 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.00042052258032599944,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003530227352907874 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 8.675800000143377,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.679126666682654 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 8.542159999933574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.543989999907355 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 0.30786265738523017,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3080238541043529 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.03548521835221448,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.035490189961945355 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 128706775.96999939,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4255231.51000012 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 128953741.82999943,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4239916.040000082 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 794534.7135465803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 442008.4236232356 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.00617321588205877,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10387411885451638 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 132686925.95203839,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5875760.076738569 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 133442494.38129489,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5819410.496402882 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 1345634.2780114594,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 613529.4856888332 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.01014142326650825,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10441704182540111 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 836.8015868999993,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 45.71982290000088 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 841.8965481999976,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 45.75324890000161 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 19.641332735950805,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.6596564729857717 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.023471911434482035,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.03630058840376085 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "a37bcf4f0c52b7d4ce225eafbdafeb9cd2bb15a1",
          "message": "c_api: include the filename in the sound-load failure message\n\nThe \"sound is not valid after load\" message gave callers no signal which\nfile failed. Echoing the path back makes Dart/Python wrappers' error\nstrings actionable. Also documents the soundInterface.cpp:39-51 invariant\nthat the post-create isValid() check depends on.",
          "timestamp": "2026-05-17T03:23:41+02:00",
          "tree_id": "77c47d29e617b7535400c98ab44277701df83ccc",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/a37bcf4f0c52b7d4ce225eafbdafeb9cd2bb15a1"
        },
        "date": 1778981340324,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.355420107005358,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.35312944549686 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.327013604512748,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.32291037255217 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.05491142818283291,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05513109803367259 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.0048357020405574505,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004856026551827959 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 45.44545041622931,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.43402675038582 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 45.40446110986249,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.399727276657536 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.0870312458904589,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08202498707528395 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.0019150705976803042,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001805364678018274 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 90.01759878532444,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.00767636458892 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 90.00665690668262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 89.99914816590154 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.0299077779940615,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02818866276902444 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.00033224367676576335,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00031318065200174977 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.404623647888677,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.403666027262082 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.329018160188504,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.32752229596323 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.13857613158781537,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13869348115632804 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.012150872827221248,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.012162183706955428 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 45.374048512540476,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.36985091760696 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 45.380122908225424,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.37601217499256 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.022773874423798898,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.022545731973141424 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.0005019140934162985,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004969320268229482 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 90.85111099628466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.83945719568935 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 90.09810917732288,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.08680028288313 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 1.3612564272696022,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.356583316693798 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.01498337678363967,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.014933855381493546 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.42294517656149,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.421556999681124 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.429616974040792,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.427770302596949 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.02980049171802096,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02974552139750369 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.0026088273433341855,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0026043315634054222 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 45.88471026517694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.88007810923637 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 45.863553283764624,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.86068505079089 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.2939756088641138,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.29400869307593547 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.006406831538548894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006408199488586898 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 90.08019350516004,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.07103164381742 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 90.1155481889133,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.10848783411194 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.07638172018168149,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07720820570116611 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.0008479302409281142,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000857192421271282 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 96.40039838447821,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.38951786343836 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 96.40580553571783,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.39616785593961 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.021282768426546462,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0227197214905337 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.0002207746937067978,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002357073880452674 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 348.66055834260237,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 348.6170084843828 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 348.64278185322297,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 348.5973031099927 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.07106804477827869,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06957518711126352 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00020383161524236847,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019957484981510975 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 675.263610333341,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 675.184783 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 675.2536390000046,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 675.1901390000015 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.0197049630896119,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.030768637351879204 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.000029181141687591646,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000045570691352324516 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.143455389021495,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.141547698102194 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.139192621109467,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.13742181303351 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.020618385932624637,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02056558711869521 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.0013615377338234366,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0013582222589618663 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 53.66887974229767,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.66122100150253 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 53.63356175710041,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.62787691620955 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.11640601243871108,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11528007365467975 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.002168966689777369,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002148293898333992 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 105.35265231330418,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.33379284272206 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 105.35980232961028,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.33075048881302 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.03997565381669217,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03990867869077299 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0003794461073254247,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003788782081583465 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.079195354864202,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.077462938003585 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.077662833084267,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.075789720619227 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.018230208809954778,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.018377650340957275 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.001208964296896262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0012188821432706281 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 53.53154672878409,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.52637135821002 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 53.55749418873162,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.55440321059566 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.053737119431570037,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.052935417793623256 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0010038402160099625,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000988959581051516 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 105.34375721236609,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.32916682154233 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 105.36151333149901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.34798597528719 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.038668622929617064,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.035089433682116485 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0003670708540579549,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000333140712501486 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.113196148323496,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.111233518235606 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.099597220160554,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.097719576186071 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.024497558029393764,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02436039737714694 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.0016209382706987017,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016120720620027365 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 53.65481444588041,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.647663558701566 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 53.64509508062773,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.63826255066923 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.03723465177315511,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0374637075012979 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.0006939666488030128,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006983287810904366 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 105.46853614110506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.4569463379495 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 105.44711399795926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.43054794895541 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.04622733715837165,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04643439362542336 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.00043830452995502527,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00044031612177180575 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 134.9929734603412,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.9774473012743 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 134.98419833319107,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.96460464414346 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.0178636335975982,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02294880524267827 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.00013233009940955372,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001700195529069071 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 539.9061277681922,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.8442128891858 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 539.9023074150954,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.8447976082382 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.012045137539107341,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016381376740905246 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.000022309688517332228,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00003034463711898281 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1083.613024574305,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1083.4429253957605 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1080.356638837409,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1080.2155642167393 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 5.650147496533027,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.669398447369754 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.0052141745885277415,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005232761518377937 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 2163.589285224933,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2163.326636862412 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 2162.30630821471,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2162.1318300525377 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 2.241293067983836,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.2279608925328698 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.0010359142944964365,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0010298772522693105 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 5582.858954946304,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5582.223469875687 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 5580.231024864627,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5579.525908511292 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 6.59335363621873,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.5157580663629355 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.0011809995003325576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0011672334691588471 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3603.1621756101504,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.662205636332 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3602.45453094836,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.022565950319 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 1.3230086438665465,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2850462378876235 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.0003671798768376312,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003566935128908784 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3603.1281853280884,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.7034731874664 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3602.8313719432485,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.40066409266 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 1.1428475989816262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9347735501767623 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.00031718205409268904,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000259464470815781 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3602.387569085868,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.904387351171 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3602.0219084437413,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.568281144607 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.8551242161819491,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6806873807859035 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.00023737707278368805,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00018897985831502846 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3602.204906940009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.8323513621403 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3602.1713055155446,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.8108337578583 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.3823641648829225,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.31535215435205854 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00010614725557290192,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008755325722831012 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3602.293564368285,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.908078333645 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3602.194844426874,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.6998775178104 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 0.52651770978343,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.46135852000146693 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.00014616179952445452,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012808725541238846 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3602.397135443595,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.9455061540116 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3602.219984966739,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.7115570748424 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 0.3475167417163591,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5106425202102763 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.0000964681928866697,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001417685301839884 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3606.019013604932,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3605.581081428798 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3602.731480004506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.174493225013 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 6.572426747609131,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.753084069628453 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.0018226267589861336,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0018729530461570926 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3602.9849617723407,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.5758163695104 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3602.932206713415,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.319254882699 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 1.2842387123043733,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2882215123789336 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.0003564374333865231,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003575834564051274 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1675.0934154175081,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1674.881474966866 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1676.0916062823926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1675.85453789385 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 2.4505201979049342,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.5008108312874744 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.0014629155456945995,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014931270472956588 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1690.4258684365798,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1690.1728490431603 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1680.5240171728763,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1680.2175334989063 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 17.958388163036552,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.92068471129973 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.01062358811371343,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01060287101490536 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1275.5886290525602,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1275.4329382729527 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1273.7224820586016,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1273.5006649020897 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 4.135185795341018,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.123762900753333 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.003241786341739668,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0032332259713609623 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1285.416359997054,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1285.2640166047172 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1274.7085404831082,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1274.4981818115582 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 20.030880744563806,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 20.092357832069734 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.015583184848067214,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.015632864199487766 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 3324.26217537828,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3323.7853429748516 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 3305.80129518267,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3305.345394643682 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 34.24041546620291,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.118748821665264 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.010300154939586428,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010265027762330865 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3338.920399644332,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3338.527210106972 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3334.4689124843358,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3334.0772116941225 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 7.799410899053288,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.821717276392897 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.002335908007835136,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002342864617881092 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1444.9805456105505,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1444.759930388517 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1442.8302818193733,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1442.6366276493309 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 4.15448380250785,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.049471577872191 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.00287511400421827,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002802868139333868 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 442.79565509209874,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 442.7356682456884 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 442.71494942106045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 442.6295075245101 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 0.34001151610408314,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.33538143072538223 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.0007678745538579479,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007575206941295462 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 1085.6586361875482,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.5157892289833 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 1085.4395412630963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.3095823324218 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.5353254731557501,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4917776257314463 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.0004930882096011553,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004530359029422728 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1591.0870069553184,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1590.8780362062907 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1591.313344675293,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1591.146581723419 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 0.5063250272086584,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4861351785574041 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.00031822585753971735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003055766485510561 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 123.15310087900245,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.1332101713882 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 123.12116882077795,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.10174965665401 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.05762927271665332,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06878616726929436 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.0004679482067875328,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005586321283555543 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1810.8487341318887,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1810.6384036200418 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1811.35725246543,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1811.1162790697615 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 0.9097944160878216,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9000091614566249 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.0005024132601136186,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004970673104343863 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1630.7293525780206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1630.5507037464956 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1630.9587858858667,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1630.7226698586062 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 0.4992965091263261,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4325173315743793 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.00030617987487438557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00026525843727557186 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 1089.9223808649465,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1089.7735213192825 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 1089.7300463275617,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1089.6556811145836 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 0.8789175311911759,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8717561316880217 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.0008064037830782774,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007999424785368904 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 7212.296504485302,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7211.086746144539 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 7212.988037144739,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7211.721662857496 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 3.5635733581166615,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.5498949238789 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.0004940969018537283,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004922829316644787 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 5772.753991044261,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5771.801704789216 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 5773.801968841807,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5773.01202264247 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 2.169128361058769,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.124169569235961 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0003757527801156801,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00036802538927721784 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 1088.7439348688154,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1088.5704398469734 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 1088.6448483549402,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1088.5136945218867 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.3526556589889071,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.34473233810682774 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.00032391056123899246,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00031668353786576154 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6134.569883524942,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6133.946603713553 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6133.190173354379,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6132.66120936592 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 2.7941323142194925,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.6631736176696346 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.0004554732226171944,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004341696773260665 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 11.044809999892399,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.04833999998315 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 11.189630000103534,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.19359000000486 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.34808004021597694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.34441771511766434 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.031515258317650376,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.031173707101536482 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 43.047656666696334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.05358666660671 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 43.87637999968774,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.883049999919876 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 8.216888185541674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.216545717258862 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.19087887290038347,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.19084462767028404 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 11.189429999944878,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.194593333338311 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 10.909110000056899,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.914679999984855 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 2.255641844155741,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.256240261673284 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.20158684081019793,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2015473179319553 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 17.234873333222822,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.23929000002992 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 16.468809999992118,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.4740200000324 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 5.351665967027383,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.3520670675324125 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.310513797435995,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3104575111575432 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2669.36638333334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2669.1545366666483 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2637.5894099999186,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2637.4650299999303 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 56.473339974754204,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 56.48857860760262 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.021156084203111065,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02116347248973749 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 567.3188233331908,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 567.2812533333152 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 567.5543799998195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 567.4397199999248 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 0.9170330149757903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9054407566553108 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.0016164332598518584,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0015961055496457668 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 33.513273333483085,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 33.51537999999247 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 32.653310000227975,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.65571000000023 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 1.5001041403579876,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.499934630976704 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.04476149272053454,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04475362150084651 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4863.790668380317,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4863.314255612259 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4864.973199958502,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4864.568629085454 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 4.9839992039198,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.076489717926678 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.001024714989549399,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0010438333718756536 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 16081.44119666671,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16079.668313333334 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 16271.562249999646,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16269.608140000004 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2411.098027901574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2410.6306719938607 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.14993046944084437,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.14991793518495372 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 32.0266599999286,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.02337333334526 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 28.88250000012249,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 28.869799999995394 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 7.733283544373048,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.735905112147184 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.24146394111625405,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.24157058757117103 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 8.44930269425145,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.448433248078143 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 8.44363663525034,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.442523405365764 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.014306752598283285,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.014141458181096642 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.001693246545424038,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001673855703874272 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 8.442000137813446,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.440907865985293 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 8.441880972328704,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.440982855872955 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.004964738221010443,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0051080352954587715 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.0005880997559775397,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006051523576086943 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 9.280880000043604,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.281290000065686 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 9.14985000008528,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.148560000085126 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 0.23034507315164524,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.23119535831353202 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.024819313809742505,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.024909830240397165 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 126258786.50333351,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4134634.846666832 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 126739435.95000027,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4143205.240000043 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 2788445.410791259,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 438909.540848413 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.02208515928289583,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10615436601426687 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 128195711.63666654,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5481177.75999998 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 129357625.73999965,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5519505.529999833 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 2904694.774854295,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 440122.8443400178 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.022658283477428695,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08029713021750629 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 817.8914869333349,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 41.04052856666555 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 821.3005178000003,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 40.74481609999906 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 8.884957576766931,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.6813814726426142 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.010863247409605488,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.04096880647897617 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "a99d73202cfb6cead73d649424fa8193677e1ef2",
          "message": "c_api: M2 surface — reverb zones, device enumeration, global reverb\n\nAdds the C wrappers needed to drive Demo04 (Channels), Demo05 (Reverb),\nand Demo06 (Devices) from a non-C++ host:\n\n  - yse_reverb.{h,cpp}      positioned reverb zones (create/destroy +\n                            position, size/rolloff, room/damping,\n                            dry/wet, modulation, 4 early reflections,\n                            preset application)\n  - yse_device.{h,cpp}      read-only descriptors (name, host, channel\n                            names, sample rates, buffer sizes, latency)\n                            + owned deviceSetup builder\n  - yse_system.{h,cpp}      device enumeration (num_devices, get_device,\n                            open_device, close_current_device, default\n                            device/host names), get_global_reverb,\n                            underwater FX\n  - yse_enums.h             YseReverbPreset added\n\nSymbol count rises from 80 (M1) to 134. Same one-DLL build; no new CMake\ntargets. The string-out functions follow snprintf semantics: returns\nfull length, writes at most cap-1 chars + NUL.",
          "timestamp": "2026-05-17T03:35:46+02:00",
          "tree_id": "ee1293c7e4dd88a17f09eb3da914f7405409866f",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/a99d73202cfb6cead73d649424fa8193677e1ef2"
        },
        "date": 1778982062697,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 13.038025089488434,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.036418767655137 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 13.041293939525284,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.038689307662446 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.006320207359468789,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005461518111062071 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.0004847518942546207,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004189431321900098 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 51.622551640464145,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 51.61812235897074 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 52.08953283427301,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 52.08613178154619 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 1.7402191324613299,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.741266454343866 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.033710443927325466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03373362638482821 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 93.54705625822709,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 93.52925253519341 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 92.91935676288693,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.90929352907587 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 1.1194510668546112,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1114894242296847 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.011966716128025245,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.011883869421617057 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 13.376130231683796,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.373295833846056 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 13.376314630062408,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.371475216978062 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.004932195339874317,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0033017334273489195 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.0003687311094049844,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002468900313259104 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 53.50597466276482,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.50089323038666 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 53.50071868057385,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.495564722902195 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.03494467589915835,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.035916921005224665 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.0006530985767366388,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006713331093475108 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 93.8746647958567,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 93.86539273402879 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 93.3030035813423,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 93.29622198575971 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 1.0535180017211516,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0515408912136737 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.011222602008882487,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.011202647329173334 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 24.293775890851347,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 24.291431421195117 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 24.184538971243764,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 24.182223628660196 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.2068396287766745,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2072933389937188 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.008514099648649805,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00853359916916416 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 137.16289035704068,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 137.15045747716889 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 138.38883905065225,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 138.38894303348198 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 2.1889988323429743,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.197732743375606 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.015959118582620415,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016024246537722688 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 242.2437832721989,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 242.22368267293663 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 242.41027999565162,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 242.3922563318043 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.5345399889954946,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5287507399662549 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.00220662004933623,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002182902737385107 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 96.44688000040621,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.43833932235519 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 96.44265750629637,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.4420025266309 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.01621862936516146,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016599876527168322 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.00016816126519689543,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017212943154984773 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 349.1210865773707,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 349.0809712138932 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 349.1072223161894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 349.08436462779173 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.04737622398947506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.046613774754016686 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00013570141080256905,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013353284366066155 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 677.0557599999922,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 676.9841673333339 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 676.7916270000002,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 676.6539499999985 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.6409362688621606,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6345376442131146 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.0009466521174890646,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009373005674749858 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.616740038371441,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.61543029836116 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.541485881062696,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.539467867897677 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.14737637026745898,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1482153255716845 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.009437076490057768,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009491594066878816 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 53.97970119552517,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.97573204511668 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 53.44868126101289,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44291546986437 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.9452774526973279,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9477123304735134 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.017511720735047153,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.017558119076205383 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 105.27830192480975,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.27050071672492 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 105.27172136533072,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.2630162845756 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.08997395317102569,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09368932535112391 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0008546296010291418,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008899865082169118 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.552495994110814,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.550585190075617 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.564683007020967,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.563005057216222 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.026274670085807694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02517931479077126 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0016894182191563975,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016191876050324263 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 53.62678078537598,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.62230848450247 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 53.46830124651362,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.46000639903758 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.36559747842057216,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3654960399627043 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0068174422008242305,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006816119079773251 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 105.2469756953901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.23459944289391 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 105.25827832776672,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.23898486485587 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.04080846689516117,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.037670888043882834 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.00038774004312742077,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003579705557232167 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.487825005981046,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.486648245328224 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.45084326505588,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.449059935394423 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.06724427705353665,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06674609586977565 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.004341750828639169,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004309912307197304 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 53.4383444479123,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.43227512726583 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 53.44865452308607,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44403685218998 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.024449103388606633,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.025858144457307237 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.00045751985098336633,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004839424186171729 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 105.78645579029201,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.77437249336077 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 105.51154683794539,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.49822626433995 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.4871078696452675,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.48672157054806037 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.004604633608397808,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004601507520913072 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 134.93899295154503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.92694619841552 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 134.94553473018175,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.93055737842616 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.012053300374578703,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01716910477215416 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.00008932407238956422,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012724741243981243 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 539.8165150148166,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.779710784765 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 539.8110396481835,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.7680787822984 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.07267067331500557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.049729635648573696 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.0001346210634422901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000092129501452127 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1079.6617292200328,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1079.56816470942 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1079.6799321873889,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1079.55742565987 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.09794846134062736,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09634036754407731 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.00009072143495480487,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008923972630297836 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 2161.683492649626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2161.4348017603 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 2161.6311148436594,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2161.489681490903 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.15277343709710836,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10612055087733961 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00007067336065459352,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00004909727130835207 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 5577.032009005841,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5576.292180414409 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 5576.869810749761,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5576.185881097306 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 0.5722942645407088,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.675729343209106 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.00010261627754988009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012117897006589217 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3601.895777035499,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.5669320925085 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3601.856515629579,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.6261819342385 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.22182925382421162,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1162209991618565 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00006158680527030289,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00003226956526234323 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3602.4512489512035,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.189706081681 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3602.472516895465,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.2199266019074 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.06369904156404242,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07742543328629761 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.000017682138400231614,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000021493991045384984 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3610.3877150450844,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3610.0465682868307 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3603.9619520179513,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.5790787570313 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 11.967502565276968,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.002540578089679 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.0033147416592978094,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003324760595480505 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3602.126181310959,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.8610676987687 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3602.222049738293,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.7945174587517 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.6844247850040633,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5873304917750867 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00019000577729761083,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00016306306121638737 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3608.801636823607,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3608.477110555059 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3604.778194791923,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3604.7791008968065 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 8.160316535904661,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.971884127170477 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.0022612261235525274,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002209210113555143 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3602.3292518687745,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3601.93953633598 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3602.266041494127,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.034638406907 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 0.156986998557289,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1751325805200339 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.00004357930316221069,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00004862174357823478 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3603.2906091108125,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.968576957123 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3603.317529179097,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.036275962891 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.5760781625277519,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6235732505107755 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.0001598755762499854,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017307207575965392 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3614.7057414091355,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3614.3784462443286 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3603.915230499071,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.450226302575 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 19.625032663840063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 19.868326108143354 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.005429219988509924,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005497024288861719 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1671.4612876838312,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1671.3358662311332 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1671.5827696042722,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1671.454253715121 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.26397745138648804,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.23653846063719206 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.00015793213598879429,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00014152658685569097 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1683.8139189073563,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1683.6554158473448 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1683.9097605539212,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1683.868790861048 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 3.8078244786537563,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.7966139991364325 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.0022614283181152765,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002254982797192907 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1272.0995614983105,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1271.9439149189243 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1272.0109616324505,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1271.8033430071396 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.23633562563609853,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2791683081535523 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.00018578390622015195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002194816177656284 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1272.6749862723937,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.530925029608 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1272.6066554420927,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.5174095340722 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.27874530555339383,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2672123629606465 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.00021902316660581662,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00020998496594841436 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 3316.002979985358,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3315.7187879706958 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 3315.767442632087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3315.521413707655 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 0.5118682792564788,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4231299895303611 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00015436303355153774,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012761335221354143 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3335.303496330278,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3335.069753559766 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3335.343117596962,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3334.960988364642 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 0.33331737244984855,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4167278290381346 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.00009993614458671795,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012495325730243882 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1448.1150702350578,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1447.922163701612 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1442.7521632828532,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1442.6542764099586 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 9.682512727141555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.565580128688618 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.006686286833248612,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0066064187485287305 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 449.82865876754613,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 449.7832741750837 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 446.48089465151025,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 446.45104952694345 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 5.919428151819408,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.907923999346025 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.01315929529265128,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013135046006726991 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 1090.0219134459173,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1089.929546443714 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 1086.956465845052,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1086.8279233803726 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 7.496335867420747,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.56331649170166 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.006877234094975547,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006939270998184876 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1586.3376066919925,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1586.2369038021136 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1586.204053823758,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1585.9966229489312 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 6.762481232494743,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.7614280056966765 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.004262952100465311,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004262558757452902 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 123.50133811136546,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.40513532272576 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 123.51659287972542,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.48663514151822 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.2634843759860759,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.15361779219326166 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.002133453612854647,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0012448249563644543 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1814.284875464632,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1814.0667292641638 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1815.2784889691536,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1815.0047129181055 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 3.292303012473593,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.3947069223593207 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.0018146560427179034,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0018713241732492985 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1638.6848454662734,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1638.530126286496 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1631.2580866641968,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.107471523848 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 13.536411592384864,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.59916649588878 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.008260533823715931,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.008299613341079926 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 1036.3604028366944,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1036.2540013917803 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 1039.968438460201,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1039.9426224461001 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 10.739415570445168,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.772383500094003 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.010362626303600141,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0103955048526961 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6855.848253806361,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6854.924824335263 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6852.661930008228,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6852.009466782922 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 5.79471078847466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.313988723270813 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.0008452215647068097,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007752074398257346 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 6149.180934459265,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6148.454413373581 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 6134.913202922967,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6134.284757778608 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 25.498842958996903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 25.5874467469835 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0041467055906754145,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004161606320334412 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 730.4020715443539,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.3359788830799 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 730.3028264232206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.2490367203553 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.6237046096648948,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6300660996288484 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0008539195519341022,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008627071893574562 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6136.4845814205255,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6135.752537641205 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6135.734292322454,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6135.687686161786 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 5.313123273133983,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.305944304224417 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.0008658252461385727,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008647585233716427 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 11.060876666798928,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.063233333364527 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 11.049459999981082,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.050610000040708 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.30130726237693206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.29944462162623403 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.027240812048954153,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02706664612443527 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 43.60489333331922,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.56961999998058 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 44.01278999978331,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 44.02128999998922 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 9.63580223575775,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.571201294200156 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.22097983733387264,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.21967603330495936 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 11.615496666953122,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.620123333292062 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 8.877160000224649,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.884589999951231 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 5.223996976577981,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.224414900377829 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.4497437454775917,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.44960064110590775 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 19.87703333346265,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 19.882799999966966 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 19.307130000356665,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 19.313990000000558 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 1.3964517043269231,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3952378328916948 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.07025453350606502,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07017310604613097 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2713.7743266666807,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2713.7544666666236 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2710.223770000084,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2710.0423499999238 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 9.421215241303534,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.468678867262048 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.0034716281117139094,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0034891435402749156 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 572.3108966664843,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 572.2163999999926 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 572.5876399998242,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 572.4838599999771 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 0.579461417969778,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6827416796912744 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.0010124941204945478,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0011931529395020541 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 32.29153666666207,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.29504333333466 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 32.32027999956699,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.32362000005651 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 0.19045869635800503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.19188759894998852 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.005898099502791251,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00594170433429714 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4805.177108726847,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4804.779885777816 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4818.371231423885,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4817.910077540807 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 33.05472899022935,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.994897781710506 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.006878982447951299,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006867098715463667 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15644.205819999872,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15642.462406666671 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15716.785639999673,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15714.210940000014 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2325.2703665030367,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2324.895864235886 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.14863460588906108,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.14862723040619472 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 35.174716666877735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 35.1742100000744 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 37.72343000036926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 37.71582000013041 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 7.621124529812611,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.618861675103768 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.21666484486537574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.21660363303362468 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 8.790264626574153,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.789584367554069 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 8.79021765690608,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.789372403696307 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.002882897136741078,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0028133374691961363 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.00032796477230340626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003200762802370166 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 9.14036818409321,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.13963573941579 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 9.140448334305992,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.13937048909184 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.0005483657218689027,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006034151418219939 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.000059993832942442294,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00006602179332155357 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 9.105473333193004,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.10742666671164 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 8.542979999788258,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.54319000012538 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 1.0268342158614219,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0279164075347713 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.11277109693114033,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11286573531158767 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 127247728.03333318,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3942860.6633333624 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 126416490.64999968,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3968244.7800001334 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 1929656.1081072937,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 303255.38002244243 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.015164562369253546,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07691252770927622 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 125660419.34666674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4827487.779999918 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 126952130.57999979,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4836842.909999746 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 2701417.0841072854,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 321561.0440054061 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.021497756398971803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06661043148314538 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 801.2300302000027,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 35.004067999998746 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 797.2078506000003,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 35.189072199997895 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 15.962924975588365,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.2112854033292912 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.019923023818270647,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.03460413239196469 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "ef4b22b3e7ffcd57a3b9ba0ff9c80e672ed84d65",
          "message": "c_api: M3 surface — DSP buffers + buffer-source sound creation\n\n  - yse_dsp.{h,cpp}        DSP::buffer + drawableBuffer + fileBuffer +\n                           wavetable, with bulk read/write of float\n                           samples, draw_line, file load/save, and the\n                           band-limited saw/square/triangle wavetable\n                           generators\n  - yse_sound_load_buffer  new factory wiring a sound to a YseDspBuffer\n                           (the in-memory create overload from\n                           sound::create(buffer&, ...))\n\nDSP::buffer is non-polymorphic so the subclass-specific entry points\ntrust the caller and static_cast — same contract as the C++ API.\n\ndspSourceObject (procedural sources subclassed in user code) is not yet\nwrapped — needs audio-thread callback plumbing, lands later alongside\nthe patcher callback bridge.",
          "timestamp": "2026-05-17T03:49:54+02:00",
          "tree_id": "647cdf4c14fe2b8ad89192b8e607243bf09b5edd",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/ef4b22b3e7ffcd57a3b9ba0ff9c80e672ed84d65"
        },
        "date": 1778982919763,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.863861989223368,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.862740533155119 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.871096991127528,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.870283263401149 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.01945674387451869,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.019239157185989408 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.001640000860781453,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016218138744768104 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 41.76424408036165,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.75855412638537 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 41.7526469257655,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.75067214780152 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.020653177899428287,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0204777007926284 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.000494518178269622,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004903833770357832 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 87.25653928073503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.23849875915671 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 87.26063506938561,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.23102115717614 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.010973362102618319,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0183223852618303 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.00012575976761252408,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021002637049513816 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.882134619547244,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.88009469771571 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.842387523218049,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.842211486493463 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.07005505807188815,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0676395394663591 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.005895831036675928,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005693518544036921 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 41.768988768379494,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.765819609382646 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 41.773397892998936,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.768697967486155 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.009021645566538736,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007277433370734836 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.00021598908263176385,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001742437581447574 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 87.30483467454206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.29883889490799 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 87.30259911008126,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.2995301164321 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.05008823823824882,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.045140635140663236 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.0005737166609955731,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005170817357033167 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.86200275836576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.861215033917835 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.860145174663202,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.859334784567245 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.006955188019389872,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007203564335215343 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.0005863417975084079,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000607320946009016 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 41.80995488253585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.8073627063409 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 41.78750715658821,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.78408369740285 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.049511049416500204,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04907608797448099 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.0011841928448763077,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001173862324662676 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 87.3020019335779,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.29775572101732 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 87.2917239677983,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.29008442287278 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.024994936616859854,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02425697894094196 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.0002863042778317591,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002778648630837824 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 79.8986037998449,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 79.89312117414725 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 79.9138146867548,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 79.91150396508014 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.03638230927046266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04052460054172465 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.00045535600799238607,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005072351655080672 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 306.6588960851314,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 306.63992718305497 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 306.6754038209569,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 306.6562104675703 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.1327179128631906,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13869506927337508 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00043278676913500287,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00045230596859155344 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 597.6409830000003,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 597.1216959999997 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 596.5424269999744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 596.521291000002 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 1.9882397289394576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1452787595667009 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.0033268128951917213,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0019179989058155098 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.765857002834503,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.764861546505372 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.769702111212498,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.769577802539033 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.015197973410446408,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.015863178925945885 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.0009639801634452222,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0010062364886079388 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 60.48073199853483,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.47497186485223 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 60.480474925094086,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.473090453829364 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.007054504352124953,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00841687112972644 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.00011664052532128496,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013917941373393653 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 124.1065763285808,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.0979301790412 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 124.10508900487321,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.09778066191188 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.06855204608759291,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0651746096497836 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0005523643316539213,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005251869193608104 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.750928469776888,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.749474863416127 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.736831646300674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.736600878694764 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.03223225334152225,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03158443198501039 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0020463716410984894,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0020054276259316235 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 60.536722063089606,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.532573696011845 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 60.485194182976244,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.48289282421209 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.10641489045435283,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10319907246137029 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0017578568318160725,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0017048518865169198 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 124.03673117070291,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.03025552761228 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 124.04411982194995,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.03925621813188 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.07797982317690673,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07877128868635039 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0006286833137321934,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006350973667776883 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.776629124703824,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.77533807159857 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.769440147536045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.767628694000207 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.01497646638873732,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01462039794847114 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.0009492817680100263,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009267882489816971 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 60.48954459084724,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.48533531168564 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 60.45856162447853,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.45391150923182 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.05512916654349833,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05546205367553315 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.0009113833955337795,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009169504209529942 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 124.17755269995097,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.17052619751813 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 124.13885707184653,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 124.12661654699752 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.17285320620947262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.17299920074042005 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.001391984319638962,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0013932388469162973 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 122.65924712857087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 122.6495508464073 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 122.64963635215474,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 122.64807813694473 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.04081199744897228,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04047127721207167 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.0003327266260341002,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003299749320953765 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 513.1729702585923,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 513.1405474073209 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 513.0371271597818,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 513.0136568630386 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.36183263983264125,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.34630929209391653 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.0007050890456103148,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006748819477308292 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1018.9250126280052,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1018.8673785101856 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1018.8091555689726,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1018.7405566861581 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.6267117880460589,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6292319887695079 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.0006150715511729834,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006175798754982098 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 1915.8824326014299,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.7941881549223 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 1915.9132986747693,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.732293574464 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.10669426454876262,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.14110582128948843 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00005568935897798836,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007365395623492609 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 4968.138377409203,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4967.913312188261 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 4967.374594659569,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4967.244914177892 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 1.5250653399772576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.271337833877501 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.00030696917519687777,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00025590982651778663 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3204.463198252282,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3204.333562180962 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3204.3951923692803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3204.2817754913576 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.651607923070656,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6298586661632833 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.000203343862218809,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019656463783832275 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3295.4822311449593,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.26014124705 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3295.429267420212,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.2440960078106 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.09417181011323406,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06335280867843825 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.000028576033341413488,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00001922543470406047 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3295.5746585316933,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.327367053313 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3295.489115697386,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.2456500452067 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.19078148224709443,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1422030777683342 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.000057890201866066977,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00004315294413237383 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3296.006968641197,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.8028722101685 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3295.6485262265196,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.4992890102567 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.7298174300321847,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6483078255909628 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00022142472299840348,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001967071001295071 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3295.32144387265,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.0963382156133 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3295.1280219961754,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.002900174666 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 0.3775385372365106,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.29632494272849513 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.00011456804553574254,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008992906801898343 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3295.6192413391095,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.3633796804065 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3295.6391472010396,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.169844102371 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 0.8104671426352562,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8113287679974829 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.00024592256668156206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00024620312679330916 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3297.2470409564316,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.0597149424466 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3297.4917833664235,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.1744232759925 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.5006122004133057,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5315782620827092 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.00015182732570383724,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00016122797523914073 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3296.7213030078897,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.5442769292063 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3296.003815014193,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.82813596527 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 1.3353520206644418,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2709177969636953 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.0004050545672289866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00038553032818584783 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1453.6586263241534,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1453.5739665735875 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1453.5191149727868,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1453.4833651673418 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.46011124147424837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4205728103772264 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.0003165194586556579,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000289337054768953 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1520.3530579583655,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1520.2421577709201 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1520.3285407995727,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1520.2374174244926 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.3214896298926361,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3429625419924202 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.00021145721923587567,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00022559731042802724 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1099.3940585051985,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1099.309080072158 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1098.607691945742,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1098.5581742608479 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 1.5182608821525363,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.4966181459794166 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0013809978964383837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00136141706923878 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1090.6716687515334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1090.5669091731404 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1089.6993350261055,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1089.6093908503037 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 4.174761970180091,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.090795619082134 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.003827698187997167,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003751072570305424 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 2919.638261160901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2919.4466328838266 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 2919.5541795795,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2919.289048496519 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 0.15056373083634067,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.27917273772574924 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00005156931008859769,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009562522382879891 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 2968.1913464660915,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2967.9984932570774 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 2967.8504427027783,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2967.694662147865 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 0.7485333065200362,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7265261463449999 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.0002521849905031342,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002447865617167855 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1278.3833817741483,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1278.2987421211367 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1278.3755733891765,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1278.3478552364597 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 0.11294678942381663,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12948777713540283 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.00008835126538258685,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010129696045897544 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 390.93597860156655,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 390.91175842579065 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 390.7256416294347,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 390.69645834402917 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 0.5308210248706106,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5289182992106628 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.001357820855397942,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0013530375789682747 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 962.0168937369654,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 961.9606608497097 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 962.0455363916859,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 961.9509352217364 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.0999660147056901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.08016502648186898 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.00010391295138006464,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008333503618646774 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1402.8690265842033,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1402.8088099129984 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1402.6221879042957,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1402.5860129762104 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 0.5602258065473352,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5201194291042542 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.00039934291507697585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003707700047424935 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 116.19214022236156,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 116.18349132108295 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 115.7459253964733,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 115.73668818551658 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.7920225130439039,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7872943586119417 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.006816489579486003,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006776301431983886 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1812.5589648277455,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1812.4478560208026 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1812.5745039893102,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1812.42689318725 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 0.36199972826359433,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.41336304202940105 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.00019971749073443055,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00022806892935222552 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1495.1269888638333,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1495.0051783467104 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1493.7840548416555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1493.6677274887672 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 2.8481469445316834,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.816474152285558 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.0019049532017986167,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00188392267336507 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 969.0886287902944,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 969.0186717918227 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 968.9849923260405,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 968.9164152482676 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 0.21089827777656403,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.19618665314564662 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.00021762537657657452,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00020245910513042622 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6392.548521897792,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6392.128935523128 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6391.362545620737,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6391.067427007264 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 2.5606285623806504,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.3932462374462364 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.00040056458759901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00037440518825366495 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 5342.5549550648175,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5342.1352577832695 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 5342.379787939367,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5342.129119057206 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 1.5372732282810582,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3206930776643047 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.00028774121019077244,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000247221946643921 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 966.489950205135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 966.422025629961 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 965.9397053691606,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 965.8763248251197 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 2.015630825116582,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.0098996386688186 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0020855165898918757,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002079732855176462 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 5995.579218917901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5995.158614861003 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 5996.287757829336,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5995.774814979731 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 1.6226801832003999,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.6878988775344028 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.0002706461083993926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00028154365646813445 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 7.348053333278888,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.3486333333031935 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 7.2871700001542195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.288529999982529 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.14074455645516254,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13849818420031804 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.019153992230532536,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.018846794760143983 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 43.102043333268135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 42.9935400000166 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 43.695090000142045,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.36192000010897 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 8.691946983365716,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.682323191433824 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.20165974304649434,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.20194483151260562 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 9.488270000019838,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.490233333337985 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 6.141940000361501,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.142980000021225 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 5.914323446388632,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.915478027939141 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.6233300113061988,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6233227171726977 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 17.724013333311934,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.72388666670584 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 16.617849999533973,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.6197599999407 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 2.6943847467989612,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.690734060086132 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.1520188851209516,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.15181399603173107 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2835.6028166666642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2835.4440566666503 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2842.340060000197,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2842.3629600000313 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 12.226813144554145,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.29353825781025 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.0043118920155845826,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004335665952888713 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 606.6029733335654,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 606.6091900000004 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 606.0423000002402,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 606.0494400000495 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 1.3814077485804024,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3802888611646515 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.0022772848292993433,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002275416996509154 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 34.164900000064335,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.16530333330077 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 33.36233999959859,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 33.36193999999182 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 1.4864438230828776,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.487489561019653 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.04350792254858286,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04353801710783593 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 5069.390406134714,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5069.0730225122725 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 5072.397843723936,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5071.964994771556 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 7.073751043184231,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.046427311194037 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.0013953849430542859,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0013900820287851706 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15135.629803333473,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15133.95939999995 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15675.518799999963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15673.57470999994 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 1657.8590608868817,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1657.521074401073 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.10953353659071091,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10952329331616142 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 36.08758666681903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 36.08322666669513 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 32.486640000115585,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.487310000135494 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 7.410408967966803,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.4028382440695095 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.20534509653925828,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2051600959207548 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 7.781843927005821,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.781287599985411 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 7.781718817474821,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.781529681541135 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.0009784917440208401,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006359452634382168 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.00012574034550154865,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008172751042377731 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 7.783915063287797,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.783437436563307 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 7.784252223552612,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.783702342112371 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.001425326901925159,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014622122931726297 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.00018311182616156718,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00018786202177251054 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 8.431303333319798,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.43185666658049 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 8.179739999718551,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.18018999979131 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 0.48933797157641096,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.48996436174386376 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.05803823587304571,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.058108715686051506 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 133954397.28333332,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3918021.6533332174 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 133738201.58,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3931545.6299999594 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 420087.3600133501,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 508312.6131265564 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.003136047554488288,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12973706071637317 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 135358757.35999998,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5439848.6000001365 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 135738597.55,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5417459.060000169 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 738835.1519583674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 505035.2174744526 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.005458347626473569,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09283993997083667 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 848.8039524000045,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 41.57385700000115 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 848.3404753000059,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 41.06861750000235 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 2.539923451751379,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 2.5443679446715675 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.0029923558255940163,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.06120115207668843 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "80aa66d9ddfe6588f844190af3db7002d62fc706",
          "message": "c_api: M4 surface — chainable DSP effect modules\n\nAdds extern \"C\" wrappers for every dspObject subclass shipped in\ndsp/modules/, plus the inherited control surface (bypass, impact,\nlfo type/frequency, link) on a shared YseDspObject* handle.\n\n  Filter modules     lowpass, highpass, bandpass(+Q), sweep(SAW/TRI/SQR)\n  Delay modules      basicDelay (3 taps), lowPassDelay, highPassDelay\n  Modulation         phaser, ringModulator, granulator, fm/difference\n\n  yse_sound_set_dsp / yse_sound_get_dsp wire a chain onto a sound\n\nLFO_TYPE, sweepFilter::SHAPE, and basicDelay::DELAY_NR mirrored as\nYseLfoType / YseDspSweepShape / YseDspDelayTap in yse_enums.h.\n\nSymbol count: 134 → 223.\n\nSubclass-specific setters trust the caller and static_cast — same\ncontract as the DSP buffer subclasses. Hilbert isn't a dspObject\n(standalone transform with operator()) — wrap separately later.",
          "timestamp": "2026-05-17T04:06:34+02:00",
          "tree_id": "36978c0ff6098c41147a86de0d13e48d7e95ea86",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/80aa66d9ddfe6588f844190af3db7002d62fc706"
        },
        "date": 1778983920764,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.858061705381628,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.855952076159257 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.84382251598411,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.83991065931751 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.028185354079767203,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02817706860050427 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.0023768938617493994,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0023766179569133556 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 41.77278907180327,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.76428902340736 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 41.77125707104218,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.762138243802816 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.00746906735929648,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013501092958057974 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.00017880221850779214,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003232688326261488 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 86.97759932963005,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.96184223539518 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 86.99656584215273,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.97794581173771 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.039839702398025804,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.030180303021077298 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.00045804555086695613,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00034705225010508485 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.844848482070384,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.843448117927366 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.845137613781077,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.844388110491389 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.005953876983916737,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006775388002790408 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.0005026553942778715,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005720790039629201 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 41.76264984505412,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.75982409107071 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 41.7650834926745,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.76215538688444 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.0055640504816850235,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006391835307893391 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.00013323030273051424,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015306183507751234 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 87.11727041191993,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.1095726868606 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 87.01849608028976,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.00798774273652 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.2084192752587245,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.20529833485129703 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.002392399053290441,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0023567827107739184 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.843395618870991,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.8426379913501 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.843757219345799,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.843196359482503 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.0016435810678829728,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0013560889547542601 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.00013877616865758748,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00011450902710567965 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 41.76729330891467,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.76525853813768 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 41.76983054158674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 41.76733509951851 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.021430152475713424,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.019684836874095495 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.0005130845400302598,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004713208432822321 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 87.06469810096063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 87.05832798770268 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 86.96937946662143,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 86.96371603579964 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.17322477606308395,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.17233406109054583 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.001989609794112095,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0019795241313948584 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 79.81409623719749,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 79.80740569456539 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 79.87977332555779,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 79.87393074878825 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.13713871294122648,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13814120397603918 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.0017182267219272576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0017309321456297647 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 306.3647454959736,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 306.336270774038 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 306.3472946094561,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 306.3221140559213 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.06117956823603474,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04727822199205798 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.00019969519709910224,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015433439165593192 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 603.0258563333272,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 602.9853180000006 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 603.118784000003,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 603.0723639999991 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.586504150345949,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5720200254949329 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.0009726019940706396,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009486466890972167 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.417422670633776,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.416579760605268 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.410494243359992,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.409541344840397 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.06834578283112448,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06779111649678334 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.0044330225804411275,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0043972863987648695 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 60.385160942099446,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.37988757496692 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 60.35581158489689,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.35290079271821 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.11760679882343271,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11695012148893902 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.0019476109194475851,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001936905254149988 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 123.53624390241424,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.52814547354511 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 123.39852164108794,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.38844592951237 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.24863061429283748,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.25211634393979543 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.00201261270732207,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002040962753656727 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.590084874873694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.589123326635786 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.587825169398172,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.58763859535681 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.010101021043426315,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00975966848686695 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0006479131527825084,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006260562754155295 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 60.51930691734276,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.51518346664858 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 60.463737216703315,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.46125790885976 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.11719330284213686,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11530845754174084 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0019364614172168134,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001905446714960225 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 123.74374388288231,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.73649956273137 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 123.74161579429087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.73523718020664 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.08233894247136744,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07862769303185639 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0006653988305808608,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006354446207038052 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.537502577723147,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.53603630058518 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.473587537199997,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.472924318977029 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.13814871955239325,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13765727574963643 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.008891307908805345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.008860514553795903 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 60.2939823145617,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.28817014006291 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 60.203867117517866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 60.1981751949038 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.16104283074655978,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.15810623205574287 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.0026709602611149144,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0026225083907577013 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 123.50477577781135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.49231487506619 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 123.51730551721464,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.50110963864104 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.04040197425758416,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03609337971451972 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.00032712884180502043,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002922722741980698 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 129.11049640764247,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 129.09750328689367 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 129.17324889468952,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 129.1623599260319 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.1831941973499075,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18055665325449435 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.0014188946866992575,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0013986068565032038 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 539.3438933071552,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.2889356875964 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 539.3752623770876,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 539.3010827480512 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.5640248489293943,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5854336421053288 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.001045761073646167,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0010855658319021465 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1068.0195155717386,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1067.909296945132 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1068.2521668039622,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1068.1002492224181 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.5513625939833173,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5336094625574781 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.000516247677073727,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004996767647626297 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 1915.9161740719858,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.6856972403766 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 1915.7573396604203,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1915.5710649896548 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.3466111067161582,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2484271034264122 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.0001809114153358231,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012968051271890874 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 4971.341979876209,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4970.857013688189 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 4971.429860751129,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4971.078330149755 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 0.5262212482609084,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4639973517767875 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.0001058509453566121,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009334353221166565 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3204.701848301605,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3204.3847102944596 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3204.5630190197935,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3204.327830674269 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.752719715362402,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6661303438764538 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00023487979568561756,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00020788088950007545 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3316.135906574735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3315.7144809965353 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3310.386409970343,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3309.8012062658095 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 23.736242104455986,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 23.773537986027264 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.007157801360732937,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007169959332228797 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3296.1765207076733,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.757650219204 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3296.153318207047,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.8968714989137 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.20552581304738474,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.32368776118492515 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.00006235279323064269,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009821345970732901 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3296.166490123776,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.874055103326 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3296.135203589136,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3295.889743094834 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.4406585647506458,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3996392361383918 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00013368819993497913,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012125440155080899 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3296.3750498663544,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.0265854355894 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3296.619311702954,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.257385715769 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 0.5291829875379107,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.42281461407435345 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.00016053482371775795,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012828009820754405 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3301.283043143321,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3300.9378511659793 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3299.6722759089766,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3299.1634713598864 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 5.736289208926507,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.64295676916775 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.0017375938790951692,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0017095010641216732 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3297.8193513147157,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.492933649752 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3297.575086642126,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.2710436600532 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.9063557757353163,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0345476717819617 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.00027483487698438865,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00031373764632662825 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3297.5358221719143,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3297.204224434879 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3297.1134477318506,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3296.8073958995533 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 1.5425313185003746,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.4321536103513635 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.0004677830360867437,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00043435392922827707 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1468.677550133426,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1467.5011287634732 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1454.804402800407,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1454.5990598943665 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 24.316180294343184,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 22.471621663844555 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.01655651391425852,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.015312847958610636 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1509.7115723785182,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1509.5653237968106 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1508.540923779513,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1508.3262247404082 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 2.532945299625296,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.5999337551413517 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.0016777676915032816,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0017223062256107485 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1099.3801904708027,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1099.3056954895849 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1099.3298498134488,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1099.2610315825184 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 0.43049924478935514,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.41064521884746924 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0003915835927560205,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003735496145724825 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1101.2325174448608,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1101.1473018472293 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1101.4379990394907,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1101.3499618167234 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 0.42261937140851685,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.39628200271426933 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.00038376942626894186,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00035988100960651366 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 2924.513369788119,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2924.3202134932867 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 2924.377771186873,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2924.2151656550395 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 0.23755308934025976,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2928504501516953 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00008122824528494823,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010014308583596146 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 2976.002010911518,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2975.7523510129577 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 2974.5011921226032,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2974.460558134 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 5.074768162388568,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.9391253394311425 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.0017052300851215548,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016597904519002882 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1286.068745591291,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1285.9207470921988 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1286.2586046206293,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1286.1745138028423 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 6.051427709042521,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.161351349451018 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.004705368768028244,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004791392753700752 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 671.7655636175231,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 671.6867797589038 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 672.1711854678209,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 672.0561009885681 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 1.4638715659911112,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.479070873433235 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.0021791405294847507,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0022020246906811754 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 964.8511768176464,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 964.7563829620821 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 964.8244962708667,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 964.7503886521887 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.36509435792559447,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3444063612184853 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.0003783944785451571,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00035698790627438796 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1426.1227930015891,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1425.9929500731814 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1432.2241685640574,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1432.1125182956766 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 10.570519245721078,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.630852308836511 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.007412068089503776,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007455052500989531 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 116.75260792209559,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 116.74303226815452 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 116.29804750634469,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 116.28793105674781 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.8840599384987823,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8793758780815303 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.0075720787246883635,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007532576985508102 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1809.1895101324465,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1809.0368845224364 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1809.3061269830005,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1809.1356916074267 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 0.5720876734723278,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6750758066199848 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.00031621213270822393,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00037316862491623356 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1487.285447729525,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1487.158259886327 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1487.3430335733976,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1487.158254933236 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 0.4624391014072083,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.49613978781356877 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.0003109282768234996,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003336159985094605 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 953.758049846755,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 953.6945778271794 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 953.7994763677492,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 953.7029858207839 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 0.17327458796025674,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.19826875742133832 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.00018167562306614098,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002078954437101423 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6394.624340224138,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6394.094363194336 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6394.971061505555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6393.981260538189 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 3.704664537214575,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.8966659864087387 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.0005793404491192867,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006094164028667944 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 5438.965234380146,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5438.559538873539 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 5438.581562706216,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5438.17319411557 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 0.8969105494943377,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1529386171298908 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.00016490462998823606,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021199337966036004 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 647.3916239807645,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 647.3359439278178 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 647.4932628180138,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 647.4054913970465 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.21930449457547527,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1824070353830666 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0003387508989180115,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00028178110159661734 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6035.886656136893,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6035.332080092964 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 5991.25730055417,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5990.554859310945 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 77.60773163474296,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 77.65855477334243 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.012857718518593539,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.012867320926630142 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 7.345686666819044,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.346923333339343 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 7.398220000141009,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.400589999946305 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.11900006287561539,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12159298064415396 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.01619999167853791,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016550190484822605 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 43.39457333344399,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.39769666666863 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 44.046060000368925,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 44.04977999996618 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 8.710521713709268,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.710719852050163 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.2007283640462968,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.20071848326320013 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 9.826953333534524,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.829786666699649 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 6.376410000257238,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.37869000001956 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 6.173202408776667,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.173898143175896 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.6281908745521957,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6280805832837859 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 17.611090000097345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.61317999997421 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 15.775560000292899,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.777189999965913 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 3.1948583757196753,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.195741859606143 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.18141173406654648,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18144036792963122 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2924.031950000199,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2923.823923333373 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2884.972070000345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2884.548460000076 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 71.6172650739281,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 71.70345904816166 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.02449264108551318,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.024523863586974993 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 621.3011533333201,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 621.3118333333038 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 620.7499900000358,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 620.7667500000015 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 1.3980616250997329,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3950398973691351 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.0022502157248526634,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0022453135809192993 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 31.768693333447118,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 31.76865666659978 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 31.50854000011805,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 31.507609999863462 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 0.5368713567135733,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5377861872585391 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.016899384279942593,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016928200424160354 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4883.760092772388,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4883.267195220976 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4852.081478575797,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4851.586826058875 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 80.04214852479522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 80.03352437397922 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.016389451366223294,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016389339590572528 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15488.14962666673,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15486.71193333334 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15660.955140000397,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15659.930360000091 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2093.611675613914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2093.3390158846482 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.13517506778274127,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.13517001058042413 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 33.0838733333394,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 33.07803333314041 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 33.18920000026537,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 33.17290999973466 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 3.131268861729326,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.1314631459418245 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.09464638043375266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09466896397387858 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 6.851120456496278,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.850553203310834 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 6.8524308094651465,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.851350435802934 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.006378980889309893,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0062234429000176785 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.000931085788056945,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009084584434743058 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 7.159241295734213,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.158654904069277 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 7.159011767590749,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.1581313876762955 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.0012068204988008788,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001123882957061889 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.00016856821120416138,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00015699638718763039 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 7.730840000021999,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.736083333422054 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 7.541890000197783,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.542060000105266 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 0.39834648400210254,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.40772824950776654 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.05152693420132469,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.052704738552423025 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 133134528.90333337,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3787044.3466666373 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 133760197.45999998,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3801520.49000003 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 1121990.7966938557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 483395.05025360844 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.008427496652716691,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.127644412371113 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 134003203.00333321,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5340901.666666486 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 134027478.36999993,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5361836.969999842 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 333754.98563617596,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 488489.26197705994 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.00249064931401583,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09146194640238518 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 844.7753583666668,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 40.60266156666709 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 845.1481301999992,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 40.45393430000103 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 4.341322743428777,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.9689962845892268 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.0051390262516919525,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.04849426635138821 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "8ca05db2405825e0d3a72c1e2f6cda8c10296d2b",
          "message": "c_api: M5 surface — Max/MSP-style patcher graph\n\n  yse_patcher.{h,cpp}   modular DSP/event graph: create_object by type\n                        string, connect/disconnect inlets and outlets,\n                        DumpJSON/ParseJSON persistence, pass_bang /\n                        pass_int / pass_float / pass_string into named\n                        receive objects, full pHandle accessors\n                        (type/name/params/gui props, set bang/int/float/\n                        list per inlet, connection enumeration)\n  yse_sound_load_patcher  Sound.fromPatcher analogue of the\n                          sound::create(patcher&, ...) overload\n\nYseOutType enum (BANG/FLOAT/INT/BUFFER/LIST/ANY) mirrored.\n\noscHandler (outbound message callback) deferred to M8 with the rest of\nthe audio-thread callback plumbing (io, log).",
          "timestamp": "2026-05-17T04:15:24+02:00",
          "tree_id": "2d0f0f01cc6727741a7add6ec6c1085e39cc8c83",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/8ca05db2405825e0d3a72c1e2f6cda8c10296d2b"
        },
        "date": 1778984438320,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 11.322595320200522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.318504331072203 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 11.316990570112297,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.314277660384723 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.01099209737952149,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009636470046457077 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.0009708107610196584,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0008513907637074 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 45.42101105578817,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.40670871638711 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 45.40602288251372,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.39056318359382 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 0.037798317186165956,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.030435840523278693 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.0008321769222560969,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006702939143504694 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 90.10314195668253,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.09108935332404 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 90.12431419824584,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.11694634524558 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.052083483503604144,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.051627111215446005 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.0005780429225058934,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005730545782721313 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 11.46078189051517,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.45957596201604 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 11.380088953176218,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.378891330296087 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.18385249884037938,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18345426001476772 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.016041880963857617,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016008817483547908 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 45.393677322486546,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.38795012658375 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 45.39776716111948,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.39331067546348 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.008958441155054139,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010554839188777474 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.00019734997654874772,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002325471663589296 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 90.24600593890919,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.23558249947887 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 90.10904728318826,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.10244316548399 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.329662380667779,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.32137431089422175 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.003652930423213848,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003561503145348209 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 11.444916412418735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.443445516683697 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 11.448682288831845,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.446511120319586 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.01715042224021942,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.016927761032663236 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.0014985187853018929,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014792538670266582 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 45.483096781292765,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.47767903620133 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 45.48238683820258,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.4789021178415 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.01248569382255236,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.009048652691152325 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.00027451283457215555,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019896909611304877 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 90.19348831138244,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.18407908958754 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 90.13044500670372,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 90.12189233810369 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 0.11267544652992272,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11058399137125485 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.001249263651284048,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001226203033701795 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 96.58293865701442,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.57199342345416 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 96.43771546286355,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.42412293311656 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.274553363677974,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.27681981444478065 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.0028426693937421863,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002866460602412606 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 349.04728768119793,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 349.01707593241343 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 348.9937751487771,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 348.96195720003544 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.39835758286901757,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3848308619994252 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.0011412711025930021,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0011026132775060755 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 675.6373870000137,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 675.5642136666655 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 675.658698999996,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 675.6326549999977 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.1743804975382586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1460911422246622 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.0002580977620444428,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0002162505640015236 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 15.175814581286792,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.173584745202414 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 15.1760012515074,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.172156508639816 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.0027522604458254576,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0026758409562703043 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.00018135833375423904,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017634863489435825 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 53.48593428083086,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.47904180980021 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 53.499108008475055,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.490645229727924 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.025214236048848187,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.02595933331825129 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.000471418072580717,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004854113394659618 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 105.44224590764286,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.42910913261322 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 105.33647945739393,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.33270254433215 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.192871731570352,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1858324944713047 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.0018291694179132802,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001762629846730059 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 15.130835280010137,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.128874754915225 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 15.14871347042215,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.147208374227409 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.043823573012753685,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04272320511308184 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.002896308908381979,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0028239512723311753 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 53.448010133208975,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.44266319187695 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 53.456457673465586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.451520707004214 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.036003232493233624,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0380241875626883 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0006736122150011279,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0007114949984092075 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 105.37208878334303,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.36163282994708 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 105.38750089241843,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.38198240716059 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.0462587623579021,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.048932381076883476 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.0004390039420497335,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00046442314685707256 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 15.172175041088785,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.170318411532335 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 15.17797263172052,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.176636150330884 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.07081410012919291,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06937588341527792 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.00466736640840331,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0045731329780487025 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 53.48565657144973,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.47891570653081 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 53.48893337831356,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.48366563237419 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.019672103287377268,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.017624157645073694 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.00036780147329963035,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00032955338402497643 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 105.49902025744763,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.48809461615632 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 105.5491809683535,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.53606754901506 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.09526359712174431,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09863124704107538 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.0009029808702419609,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000934998848921945 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 135.00077555105347,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.98970341246337 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 134.99910373912135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.98320667799268 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.016383835881038127,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01752073278779817 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.00012136105006924368,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00012979310528791422 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 540.2588878516227,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 540.1954080323394 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 540.3799330230604,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 540.2920607564188 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.23947051337735878,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.22222784454716393 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.0004432514092079633,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004113841792114234 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1080.173541622957,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1080.061519368941 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1080.1873187824588,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1080.101691569297 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.19403628656611313,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.14356373292919372 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.00017963436345105646,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013292181079932857 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 2162.9448548152827,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2162.7071837029707 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 2162.855034585564,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2162.7156918171922 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.48762926263231643,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3880047581565888 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00022544692322909934,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017940697708889562 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 5579.960985926398,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5579.433350071857 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 5580.043185423204,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5579.756286914463 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 1.0685071307203078,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1994496669420034 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.00019149007195843533,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021497696839170162 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3602.9800520828376,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.60568553381 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3602.8470556752345,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.6022305024057 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.31952631479733873,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.05978485829339548 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00008868389782303252,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000016594893671949824 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3603.6132979142853,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.150137797788 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3603.285488504176,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.0598313479 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.6029923608310378,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5686049279496881 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.0001673299299844521,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001578077255190965 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3603.2829367387462,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.908104667357 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3603.4024830269077,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.8894013249196 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.3121233034139715,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.18204116967376763 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.00008662192475411535,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00005052617618471699 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3603.339121424755,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.022308340244 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3603.118230429278,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.9324728393635 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.4779833482925893,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5919051780152392 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00013265011484780694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00016428018684344595 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3603.6543323397455,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.269950080701 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3604.016019299523,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.275536086092 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 0.7880153645857582,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6935270156999668 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.0002186711853892277,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000192471567578342 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3646.6548377297145,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3646.268576253911 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3604.381081595891,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.7213471806385 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 74.30634877139748,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 74.26066829808327 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.02037657855703671,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.020366209110788243 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3603.9469360790517,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.440475332484 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3603.8509549551713,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.2170913770956 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.9075904660747042,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6304976066359349 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.0002518323610674817,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001749710064456552 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3616.7736220336324,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3616.4777126995 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3603.4960215341093,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.340768723973 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 23.270182157043436,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 23.30748424065493 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.006433961477511308,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00644480239953067 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1673.9886803273491,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1673.8172658661506 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1673.9818244816868,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1673.7408447338555 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.12493356339768606,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16321137106907344 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.00007463226296921868,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009750847622223351 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1684.8670248833696,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1684.6547846888018 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1684.7020084690694,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1684.5334251343754 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 0.44107334925118336,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3615487350967575 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.00026178525826494556,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021461295120088632 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1272.9098875790653,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.7591639211153 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1272.424010918023,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1272.2563063005919 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 1.0680964796559125,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.1463558901408286 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0008390982661681687,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009006856305863368 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1285.9090649404013,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1285.7195900623299 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1273.3019111665103,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1273.2548015690975 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 22.883096204299466,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 22.645629506565623 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.01779526782118146,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.017613194728928253 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 3317.1041578962886,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3316.776328797183 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 3310.3517049565926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3309.8490159072285 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 12.485987770796692,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.655530864893146 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.00376412291458322,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003815611789982422 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3327.534450315857,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3327.2009191604193 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3324.37690324614,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3323.950628361968 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 6.130358051355331,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.152758890927173 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.0018423124216710735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.001849229740078261 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1443.5227308796295,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1443.369451423563 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1443.12212450301,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1443.0168309709607 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 1.0765914470641942,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0019380325110978 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.0007458084476495629,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006941660234826977 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 812.105651964269,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 812.0223233147057 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 816.4041676559754,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 816.2412650018572 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 12.445573491392302,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.396126956807576 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.015325066955623011,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01526574652062042 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 1085.728094677519,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.6305375603615 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 1085.7308548305984,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1085.649902058243 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.20777200939999643,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.151979387429407 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.00019136652207725035,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001399918132101704 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1582.2599294085906,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1582.0967806497338 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1581.0473320557403,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1580.9712924276466 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 2.3443786545577248,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.2219314920503734 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.001481664681626612,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0014044219792533 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 123.19326839672964,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.1786001196112 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 123.19502132371692,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.17743920507793 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.009877117981092217,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010693034852517172 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.00008017579296040838,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00008680919284789582 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1814.026621094281,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1813.7820933715077 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1811.7165999256958,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1811.6500450274023 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 4.4547501063120265,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.3617635469065155 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.0024557247696975768,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0024047891766307773 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1631.6712452454522,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.532197265478 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1631.3269284436437,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.1118419200031 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 0.7336730213291586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7940763478917588 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.0004496451251850015,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004867059008842527 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 1030.8082605110374,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1030.6894208859642 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 1029.0386726583495,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1028.9612029216723 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 5.286211426837094,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.194160962602148 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.00512821989243313,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.005039501577630757 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 7217.496251935987,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7216.602904835624 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 7218.056840474627,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7216.819628291181 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 1.241516676211095,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2592745803877456 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.00017201486954400238,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017449686465967866 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 6135.665830893304,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6134.871869502494 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 6136.232575876992,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6135.562612873248 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 1.6638013648184626,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.3013672367295315 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.0002711688365492725,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021212622926957162 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 733.1093261166303,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 733.0173401639532 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 732.3983130538373,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 732.2189807770233 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 3.1517852306608014,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.1492398985657033 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0042992022040644244,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004296269304981675 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6137.477938970379,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6136.668631569125 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6137.885000264722,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6137.08589682506 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 0.7687490055389313,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2198657149080285 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.00012525487067867105,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019878305121978095 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 11.39636999994309,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.399106666658554 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 11.341420000121616,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.342290000015964 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.5579680556927819,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5592936311380431 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.04896015623356982,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.049064689672036386 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 43.177036666482614,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 43.1851966666367 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 45.10289999984707,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 45.108879999986584 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 8.195009713100335,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.194557151315994 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.18980018884579783,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.189753845850766 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 11.383286666841741,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 11.388626666691456 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 12.390500000378779,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.395650000058822 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 1.750358913145104,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.7500298241720538 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.15376568862521112,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1536646933286874 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 15.367469999887364,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15.373159999967356 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 17.059010000366474,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.065159999987145 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 5.638491819198594,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.637282068479462 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.3669108720719755,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3666963765739401 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2679.958680000141,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2679.714373333392 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2667.9942900000246,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2667.782240000065 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 30.593126064416026,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 30.801373646032403 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.011415521549912261,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01149427489457299 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 554.862980000242,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 554.7097833333225 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 555.768540000372,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 555.7776700000261 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 4.269444861153855,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.186134940539893 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.007694593106845933,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0075465316573017135 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 32.761066666656305,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.76506999995377 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 32.04970000012963,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 32.052549999974644 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 1.2568421345182987,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2573494475007472 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.03836389539164464,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03837469132532057 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4793.543805325651,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4792.882585591425 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4760.173008275277,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4759.641016279927 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 64.96897072899462,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 65.09073448566032 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.013553432151139152,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013580707084571392 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 15360.43566999979,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15358.714926666724 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 15508.379549999632,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 15505.902240000041 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 1960.5835214791077,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1960.1150660255732 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.12763853601550448,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1276223352920174 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 37.73800333325046,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 37.73519666670684 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 37.730230000079246,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 37.72975999993378 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 6.406073537236843,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6.398216732235163 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.16975125792075318,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.16955567473907474 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 7.43702356754538,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.436334100385994 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 7.395513776007907,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.394881596111517 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.07963154464035561,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07992304688643682 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.010707448203856954,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.010747640679873206 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 7.396739768935259,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.395979479641103 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 7.387789394972685,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.38711505160813 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.01576452863722425,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.015367002783487528 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.0021312806898293127,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0020777508679936498 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 9.320456666538727,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 9.322200000004461 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 8.797109999818531,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.796520000089458 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 0.9767594675626764,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.9768935179988611 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.1047973830584214,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10479216472489257 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 126073886.02999984,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4182507.5933331843 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 124998830.28999989,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4202754.909999839 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 1887687.9008039515,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 439332.32792354 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.014972869959404344,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10504041370392851 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 129166558.19000007,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5560451.1633331785 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 129355407.14000012,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5575585.499999818 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 1562131.4963599667,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 419497.9070393238 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.012093931418859352,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07544314206113058 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 822.3713970000023,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 41.676381733332356 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 815.2037508000034,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 41.896729599997684 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 13.179631195661399,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 1.8260093407402598 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.01602637353845292,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.043814008433458514 ms\nthreads: 1"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "committer": {
            "email": "yvan@attr-x.net",
            "name": "yvan vander sanden",
            "username": "yvanvds"
          },
          "distinct": true,
          "id": "3096f7122b82461e4559c8d261a38d4416a0a1c1",
          "message": "c_api: M6 surface — MIDI file playback + device output + midiNote\n\n  yse_midi.{h,cpp}    MIDI::file (load/play/pause/stop), midiOut (full\n                      note/pressure/control surface, all-notes-off,\n                      reset, raw 3-byte send), and a midiNote\n                      convenience value type\n\nmidiOut is Windows/Linux-only upstream (RtMidi-backed). The header is\npresent on every platform but Mac/iOS/Android builds get no-op stubs\nthat set last_error so the binding's symbol surface stays uniform.\n\nChannels passed as plain int (0..15) — clamps internally; matches\nYSE::MIDI::M_CHANNEL ordering.",
          "timestamp": "2026-05-17T04:20:18+02:00",
          "tree_id": "f5f820e3cf264756198d930ecfe725d8b82a6dfb",
          "url": "https://github.com/yvanvds/yse-soundengine/commit/3096f7122b82461e4559c8d261a38d4416a0a1c1"
        },
        "date": 1778984760441,
        "tool": "googlecpp",
        "benches": [
          {
            "name": "BM_BufferAddScalar/128_mean",
            "value": 13.098590383481087,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.097219650877925 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_median",
            "value": 13.06458767688306,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.06283768493583 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_stddev",
            "value": 0.09339737094760837,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.09386143955026599 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/128_cv",
            "value": 0.007130337556428501,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00716651640976139 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_mean",
            "value": 52.425004703819546,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 52.41580791837629 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_median",
            "value": 53.12749240933735,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.1207387693103 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_stddev",
            "value": 1.228673677897276,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2229670546086011 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/512_cv",
            "value": 0.023436787175104595,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.023332027172280695 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_mean",
            "value": 92.94097773716031,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.91678678288667 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_median",
            "value": 92.93272617866653,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 92.92380661785374 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_stddev",
            "value": 0.014708469189967722,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01622378411326404 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddScalar/1024_cv",
            "value": 0.00015825601955214718,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00017460552258628172 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_mean",
            "value": 13.563352361282094,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.559982739374824 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_median",
            "value": 13.425126977779755,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 13.419982630967255 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_stddev",
            "value": 0.25813196409621486,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2607748823303072 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/128_cv",
            "value": 0.01903157547046242,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.019231210492110854 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_mean",
            "value": 52.84757811007572,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 52.835881420413536 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_median",
            "value": 53.01010920132966,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 52.99681360750582 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_stddev",
            "value": 0.7815064369780996,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7765546113299232 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/512_cv",
            "value": 0.01478793286137554,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.014697485694445053 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_mean",
            "value": 93.71306654131054,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 93.70240640162348 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_median",
            "value": 93.6400014914746,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 93.62413313011967 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_stddev",
            "value": 0.28258121874708386,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2858498365577568 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubScalar/1024_cv",
            "value": 0.0030153875993644557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003050613613193228 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_mean",
            "value": 24.223571938705465,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 24.22034485821409 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_median",
            "value": 24.297601781066135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 24.29309640049874 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_stddev",
            "value": 0.4283654080355385,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.42930888434934555 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/128_cv",
            "value": 0.017683825041140105,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.017725135082201342 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_mean",
            "value": 138.17821754562826,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 138.1599456154544 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_median",
            "value": 138.51433756317928,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 138.50556538592505 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_stddev",
            "value": 0.6593643997174989,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6609097680057866 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/512_cv",
            "value": 0.0047718403915564195,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004783656833836058 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_mean",
            "value": 151.43484576157533,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 151.4194297921616 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_median",
            "value": 117.79710211444558,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 117.76950653866805 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_stddev",
            "value": 80.80793059292316,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 80.8046499266989 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulScalar/1024_cv",
            "value": 0.5336151675430777,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5336478286677702 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_mean",
            "value": 96.45653063896003,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.44672177567197 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_median",
            "value": 96.45781090640378,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 96.44647031561898 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_stddev",
            "value": 0.002907336391466005,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007584981455531252 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/128_cv",
            "value": 0.000030141415746625398,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007864426406501782 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_mean",
            "value": 349.43248214830004,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 349.39035435705756 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_median",
            "value": 349.32542575418734,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 349.29051235991756 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_stddev",
            "value": 0.2120808205572692,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.20470548027102645 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/512_cv",
            "value": 0.0006069293250970916,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005858933359729468 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_mean",
            "value": 676.9966333333267,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 676.9257050000022 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_median",
            "value": 676.9715019999866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 676.8638820000028 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_stddev",
            "value": 0.19390630677087395,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.20605293518763326 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivScalar/1024_cv",
            "value": 0.00028642137526761085,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003043951997474119 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_mean",
            "value": 16.619131006748876,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.61730730528038 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_median",
            "value": 16.624462355911984,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.622873731874837 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_stddev",
            "value": 0.010301152020160369,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01036387170615339 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/128_cv",
            "value": 0.000619836982810784,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0006236793672859455 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_mean",
            "value": 53.562948528965514,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.55743286525591 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_median",
            "value": 53.52989683659016,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.52309180190165 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_stddev",
            "value": 0.11354493802491024,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.11398072292064672 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/512_cv",
            "value": 0.0021198410681874235,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002128196159203683 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_mean",
            "value": 105.31372892764229,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.30410394072946 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_median",
            "value": 105.3152405047842,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.30622235578228 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_stddev",
            "value": 0.02362434884734194,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.025480115965766482 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferAddBuffer/1024_cv",
            "value": 0.00022432354345342266,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00024196697955958105 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_mean",
            "value": 16.637703597771978,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.63584570382582 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_median",
            "value": 16.63989670358495,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.637981618254077 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_stddev",
            "value": 0.005406921304824248,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0046464143559831325 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/128_cv",
            "value": 0.0003249800234179139,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00027930136157217273 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_mean",
            "value": 53.54303214211617,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.53792197107324 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_median",
            "value": 53.482085530109494,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.474405628159836 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_stddev",
            "value": 0.12177830719758474,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12207995476378557 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/512_cv",
            "value": 0.0022744006516918136,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002280252020796509 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_mean",
            "value": 105.21952924513351,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.20691971999575 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_median",
            "value": 105.22118353214138,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.20560503918942 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_stddev",
            "value": 0.01141575974236534,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007985412660662288 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferSubBuffer/1024_cv",
            "value": 0.00010849468558037031,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00007590197186568303 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_mean",
            "value": 16.785360263428757,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.783325359377372 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_median",
            "value": 16.784986557056943,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16.782609515602555 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_stddev",
            "value": 0.004225474188071141,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.003652561071221147 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/128_cv",
            "value": 0.0002517356864408462,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00021763035590443024 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_mean",
            "value": 53.746031283352586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.73711622133812 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_median",
            "value": 53.5376477526866,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 53.52704979369083 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_stddev",
            "value": 0.39128058790653086,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3897770702221312 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/512_cv",
            "value": 0.0072801763881629525,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007253405050927483 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_mean",
            "value": 105.35600472641339,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.34219811200369 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_median",
            "value": 105.3360313378053,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 105.32590550749853 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_stddev",
            "value": 0.037068990444681926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.04431756197771231 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferMulBuffer/1024_cv",
            "value": 0.00035184506607802775,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004207009419965992 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_mean",
            "value": 134.98986781025914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.97348569283554 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_median",
            "value": 134.9961627310504,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 134.9775706809505 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_stddev",
            "value": 0.016907679083590928,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.01683489641363416 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/128_cv",
            "value": 0.0001252514678165049,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001247274331489519 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_mean",
            "value": 540.1147097339924,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 540.0454574656509 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_median",
            "value": 540.1513916856662,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 540.0759976625345 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_stddev",
            "value": 0.10070056046660818,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10130837179468159 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/512_cv",
            "value": 0.00018644291416577676,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00018759230430361544 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_mean",
            "value": 1080.1635733274672,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1080.0021462306736 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_median",
            "value": 1080.2117501115374,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1080.0344260954514 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_stddev",
            "value": 0.13671965568655264,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.10832021059727148 ns\nthreads: 1"
          },
          {
            "name": "BM_BufferDivBuffer/1024_cv",
            "value": 0.00012657310342857126,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010029629197990112 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_mean",
            "value": 2163.0973344535873,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2162.8839445309322 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_median",
            "value": 2162.946870289374,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2162.643360639543 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_stddev",
            "value": 0.46225862855580985,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4309241358239139 ns\nthreads: 1"
          },
          {
            "name": "BM_HighPass_cv",
            "value": 0.00021370218583926061,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019923590302362202 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_mean",
            "value": 5578.806104210111,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5578.331370658664 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_median",
            "value": 5578.536344006977,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5578.276450294383 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_stddev",
            "value": 0.7311922534665287,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5182497419670241 ns\nthreads: 1"
          },
          {
            "name": "BM_LowPass_cv",
            "value": 0.000131066081130643,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00009290407964879136 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_mean",
            "value": 3603.2472636021926,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.660475550491 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_median",
            "value": 3603.502551453914,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.563626720505 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_stddev",
            "value": 0.45086386859719996,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3813028872525062 ns\nthreads: 1"
          },
          {
            "name": "BM_BandPass_cv",
            "value": 0.00012512709664739134,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010583925125340675 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_mean",
            "value": 3603.162703273944,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.722351475728 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_median",
            "value": 3603.2960499902415,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.6123287248097 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_stddev",
            "value": 0.5816262152417883,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.43081169004189696 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowpass_cv",
            "value": 0.00016142102456636356,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00011957948684705892 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_mean",
            "value": 3602.837225418407,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.276765160917 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_median",
            "value": 3603.03332544267,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.5313699658877 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_stddev",
            "value": 0.8952072302504845,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8330337155376948 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highpass_cv",
            "value": 0.00024847284910200785,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00023125200250971898 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_mean",
            "value": 3603.5552245410004,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.974686822563 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_median",
            "value": 3603.3774594549654,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.7462799456966 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_stddev",
            "value": 0.605716678679063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4941602229960271 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Bandpass_cv",
            "value": 0.00016808863495527965,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013715339849690238 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_mean",
            "value": 3603.0632819535554,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.5164903974223 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_median",
            "value": 3602.8291963663783,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.3819098839685 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_stddev",
            "value": 0.4700441372822153,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.4761231359483969 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Notch_cv",
            "value": 0.00013045680869289665,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00013216404066921343 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_mean",
            "value": 3603.2129868838088,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.685083593258 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_median",
            "value": 3603.3060201530093,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.747511759351 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_stddev",
            "value": 0.6770297950364607,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3821998640472759 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Peak_cv",
            "value": 0.00018789613533836113,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010608750284276197 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_mean",
            "value": 3604.0544879382014,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.4396501737688 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_median",
            "value": 3603.728891542089,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3603.194641441849 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_stddev",
            "value": 0.6736889472405205,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.5992627055333495 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Lowshelf_cv",
            "value": 0.0001869252946910697,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0001663029670843665 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_mean",
            "value": 3602.998548979589,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.5075243809138 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_median",
            "value": 3602.9608070143167,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3602.4852736871885 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_stddev",
            "value": 0.1865555071213324,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12081585582393846 ns\nthreads: 1"
          },
          {
            "name": "BM_BiQuad_Highshelf_cv",
            "value": 0.00005177784686429227,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000033536600550112805 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_mean",
            "value": 1671.7976292904357,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1671.5971713685728 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_median",
            "value": 1671.9716117129074,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1671.908174239751 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_stddev",
            "value": 0.5353072951082203,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6202755589253803 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_FixedFreq_cv",
            "value": 0.000320198620771715,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00037106760501248476 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_mean",
            "value": 1698.6093569161642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1698.3408882443164 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_median",
            "value": 1687.9221177442669,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1687.700335204657 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_stddev",
            "value": 22.77146394760422,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 22.807993575392782 ns\nthreads: 1"
          },
          {
            "name": "BM_Sine_ModFreq_cv",
            "value": 0.013405945195631068,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.013429573375561171 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_mean",
            "value": 1274.7768097139885,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1274.6222346586585 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_median",
            "value": 1273.6262096759458,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1273.4363900325827 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_stddev",
            "value": 2.048310309677304,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.0673046001667115 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_FixedFreq_cv",
            "value": 0.0016067991620720393,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016218959186133542 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_mean",
            "value": 1288.4672076145334,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1288.3003906508563 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_median",
            "value": 1288.6417228231232,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1288.5506264198648 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_stddev",
            "value": 7.981235721051887,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.927893192924839 ns\nthreads: 1"
          },
          {
            "name": "BM_Saw_ModFreq_cv",
            "value": 0.0061943646480753945,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.006153761382405252 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_mean",
            "value": 3353.464914678474,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3352.9416041241943 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_median",
            "value": 3353.231468648781,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3352.688358709966 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_stddev",
            "value": 1.6818036543596342,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.5986901091378007 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_FixedFreq_cv",
            "value": 0.0005015122260555642,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004768022524374941 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_mean",
            "value": 3395.629422266282,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3395.243198320362 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_median",
            "value": 3396.1973337609993,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3395.7306836186476 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_stddev",
            "value": 3.182895918286901,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3.106287327736587 ns\nthreads: 1"
          },
          {
            "name": "BM_Oscillator_ModFreq_cv",
            "value": 0.0009373507890512385,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0009148939107729536 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_mean",
            "value": 1443.090176585389,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1442.9163877643496 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_median",
            "value": 1443.742742005348,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1443.518809915877 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_stddev",
            "value": 1.1421910653789482,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.0819479101355425 ns\nthreads: 1"
          },
          {
            "name": "BM_Noise_cv",
            "value": 0.0007914897377248994,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.000749834099404685 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_mean",
            "value": 439.65667368639214,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 439.5916307558685 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_median",
            "value": 439.9617313396638,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 439.8955726748016 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_stddev",
            "value": 0.7072008691627366,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.7117666873621359 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_Process_cv",
            "value": 0.0016085298176712864,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0016191543186076316 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_mean",
            "value": 1084.666223984678,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1084.530436323261 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_median",
            "value": 1084.543818542725,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1084.416248649856 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_stddev",
            "value": 0.26940588126633935,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.21443991824770822 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadFixed_cv",
            "value": 0.00024837675895967144,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019772604904911222 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_mean",
            "value": 1582.8015696704185,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1581.4976956895464 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_median",
            "value": 1580.6389562422416,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1580.552544599929 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_stddev",
            "value": 3.7702832839786864,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.7505826643870446 ns\nthreads: 1"
          },
          {
            "name": "BM_Delay_ReadModulated_cv",
            "value": 0.002382031554823236,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0011069144578321852 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_mean",
            "value": 123.18843985662777,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.17128548707932 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_median",
            "value": 123.16380014245657,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 123.13997284586304 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_stddev",
            "value": 0.07608243523200713,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07107663466576684 ns\nthreads: 1"
          },
          {
            "name": "BM_Clip_cv",
            "value": 0.0006176101858303853,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0005770552315395199 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_mean",
            "value": 1811.5726207729397,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1811.386203416143 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_median",
            "value": 1812.3275724637167,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1812.187826086946 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_stddev",
            "value": 1.893053050747451,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.9099558015021714 ns\nthreads: 1"
          },
          {
            "name": "BM_Sqrt_cv",
            "value": 0.0010449777331806582,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0010544166660318673 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_mean",
            "value": 1631.316001784476,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.0730447700528 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_median",
            "value": 1631.2625169051632,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1631.1133207668079 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_stddev",
            "value": 0.7726272485941594,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8122966564979239 ns\nthreads: 1"
          },
          {
            "name": "BM_RSqrt_cv",
            "value": 0.0004736220620339605,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0004980136598434443 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_mean",
            "value": 1029.7079093133805,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1029.3440528887888 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_median",
            "value": 1025.3508689236476,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1025.0907070837093 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_stddev",
            "value": 8.071717648322648,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 7.760798327284476 ns\nthreads: 1"
          },
          {
            "name": "BM_Wrap_cv",
            "value": 0.007838842039879978,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.007539557163131498 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_mean",
            "value": 6856.880418037581,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6855.839652201146 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_median",
            "value": 6857.147246592875,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6856.339867617041 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_stddev",
            "value": 1.8783214622034923,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.1254350389350427 ns\nthreads: 1"
          },
          {
            "name": "BM_MidiToFreq_cv",
            "value": 0.00027393236394533226,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.0003100181956928715 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_mean",
            "value": 6133.77077895924,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6132.90071642523 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_median",
            "value": 6133.519627248758,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6132.590963232848 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_stddev",
            "value": 0.7799388813141311,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.6231066181857302 ns\nthreads: 1"
          },
          {
            "name": "BM_FreqToMidi_cv",
            "value": 0.00012715487901660206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00010160063679441546 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_mean",
            "value": 730.5561481911542,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.4723178132855 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_median",
            "value": 730.470748089063,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 730.4060332555977 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_stddev",
            "value": 0.20738181707595726,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1456978069922162 ns\nthreads: 1"
          },
          {
            "name": "BM_DbToRms_cv",
            "value": 0.0002838684166705481,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00019945698617077192 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_mean",
            "value": 6136.780142342337,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6136.176009723803 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_median",
            "value": 6137.746612775464,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 6137.403637641747 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_stddev",
            "value": 2.360646477989896,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2.463839125278725 ns\nthreads: 1"
          },
          {
            "name": "BM_RmsToDb_cv",
            "value": 0.000384671834941909,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00040152680128052997 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_mean",
            "value": 10.731170000137048,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.735016666719352 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_median",
            "value": 10.88740999989568,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.892049999995379 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_stddev",
            "value": 0.48727320367245547,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.490528583136922 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPosition/iterations:100000_cv",
            "value": 0.045407276528676044,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.045694254453992285 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_mean",
            "value": 40.626016666465155,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 40.63596666663708 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_median",
            "value": 42.518369999697825,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 42.52643999990369 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_stddev",
            "value": 5.177747990954033,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.176597284117878 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetPreset/iterations:100000_cv",
            "value": 0.12744906874485723,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.12738954450339102 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_mean",
            "value": 12.520793333502903,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 12.529339999976704 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_median",
            "value": 10.537069999827509,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 10.546239999911222 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_stddev",
            "value": 4.776926387163389,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.778360628036222 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_SetDryWetBalance/iterations:100000_cv",
            "value": 0.38151946605343123,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.3813736899186316 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_mean",
            "value": 17.577049999886185,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.582390000020116 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_median",
            "value": 17.616239999824757,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 17.618910000010146 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_stddev",
            "value": 4.364206972554311,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4.3665845397917 ns\nthreads: 1"
          },
          {
            "name": "BM_Reverb_GlobalToggle/iterations:100000_cv",
            "value": 0.2482900698685257,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.24834988529925137 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_mean",
            "value": 2639.4967433335146,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2639.2794833333255 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_median",
            "value": 2639.9511300002137,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2639.915560000077 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_stddev",
            "value": 1.224265871889206,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.2404656914429475 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_BuildSmallGraph/iterations:100000_cv",
            "value": 0.0004638254905906939,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.00047000164221951927 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_mean",
            "value": 562.8787366667135,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 562.8852066666923 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_median",
            "value": 562.5695700001644,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 562.5726099999895 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_stddev",
            "value": 1.6368269517883691,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 1.637133018710439 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_CreateObject/iterations:100000_cv",
            "value": 0.0029079566257581902,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.002908466947293311 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_mean",
            "value": 34.03074000004836,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.0326466666833 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_median",
            "value": 34.42366000001584,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 34.427329999999756 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_stddev",
            "value": 0.812770549894846,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.8154397091512465 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_Connect/iterations:100000_cv",
            "value": 0.02388342274936399,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.023960514065734764 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_mean",
            "value": 4998.445201880441,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4997.829317677044 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_median",
            "value": 4897.741615463954,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4897.12004231676 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_stddev",
            "value": 184.00646198937298,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 184.2697544037911 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_DumpJSON_cv",
            "value": 0.03681273967355864,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.03686995747374549 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_mean",
            "value": 16037.003186666729,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16035.430826666661 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_median",
            "value": 16065.253109999845,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 16063.512160000073 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_stddev",
            "value": 2476.151429626017,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 2475.860600332587 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_ParseJSON/iterations:100000_cv",
            "value": 0.15440237810046117,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.15439938141326837 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_mean",
            "value": 38.68902666662658,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 38.69068333330006 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_median",
            "value": 38.91586999998253,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 38.92460000002984 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_stddev",
            "value": 5.921734522327657,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 5.91528480711974 ns\nthreads: 1"
          },
          {
            "name": "BM_Patcher_PassFloat/iterations:100000_cv",
            "value": 0.15305979582670107,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.1528865426377365 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_mean",
            "value": 8.883351361221044,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.882138427200632 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_median",
            "value": 8.793767996192402,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.792552302534986 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_stddev",
            "value": 0.15553323708647723,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.15618290670147736 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_cv",
            "value": 0.017508396410552282,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.017583930714610722 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_mean",
            "value": 8.812909069467148,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.811795992293481 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_median",
            "value": 8.792721996224165,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.791502063642037 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_stddev",
            "value": 0.035466194315747004,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.035395168295288874 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_UpdateTick_100Sounds_Reverb_cv",
            "value": 0.004024345881273388,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.004016793889264388 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_mean",
            "value": 8.596189999631557,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.598980000063722 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_median",
            "value": 8.506119999651675,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 8.509620000154428 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_stddev",
            "value": 0.2647373488113719,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.2647815620148828 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_ListenerPosUpdate/iterations:100000_cv",
            "value": 0.030797056466029586,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.030792205821262592 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_mean",
            "value": 129700330.65000011,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 3996070.673333349 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_median",
            "value": 129731718.36999995,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4002160.7299999576 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_stddev",
            "value": 360244.7851066989,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 289882.17810688756 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_cv",
            "value": 0.0027775163201305113,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.07254180463857522 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_mean",
            "value": 130365601.76333351,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4957897.69999997 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_median",
            "value": 131148285.45000021,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 4962421.849999998 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_stddev",
            "value": 1552894.955418255,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 335655.94286992436 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RenderOffline_100Sounds_Reverb_cv",
            "value": 0.011911845873556351,
            "unit": "ns/iter",
            "extra": "iterations: 3\ncpu: 0.06770126436249914 ns\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_mean",
            "value": 830.9312240666694,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 35.95008673333381 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_median",
            "value": 828.2061614000044,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 36.27116340000214 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_stddev",
            "value": 7.799705947638155,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.7675110383393157 ms\nthreads: 1"
          },
          {
            "name": "BM_Engine_RealtimeFactor_100Sounds_cv",
            "value": 0.009386704605304793,
            "unit": "ms/iter",
            "extra": "iterations: 3\ncpu: 0.021349351505949507 ms\nthreads: 1"
          }
        ]
      }
    ]
  }
}