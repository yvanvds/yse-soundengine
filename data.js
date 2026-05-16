window.BENCHMARK_DATA = {
  "lastUpdate": 1778942493659,
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
      }
    ]
  }
}