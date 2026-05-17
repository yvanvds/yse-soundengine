window.BENCHMARK_DATA = {
  "lastUpdate": 1778981341246,
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
      }
    ]
  }
}