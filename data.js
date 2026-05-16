window.BENCHMARK_DATA = {
  "lastUpdate": 1778961050355,
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
      }
    ]
  }
}