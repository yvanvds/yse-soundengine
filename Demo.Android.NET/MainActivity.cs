using Android.App;
using Android.Widget;
using Android.OS;
using Yse;
using System.Threading;
using System.IO;

namespace Demo.Android.NET
{
  [Activity(Label = "Demo.Android.NET", MainLauncher = true, Icon = "@mipmap/icon")]
  public class MainActivity : Activity
  {
    sound sound = null;
    Timer timer = null;
    BufferIO FileManager = null;

    protected override void OnCreate(Bundle savedInstanceState)
    {
      base.OnCreate(savedInstanceState);

      // Set our view from the "main" layout resource
      SetContentView(Resource.Layout.Main);

      Yse.Yse.System().init();

      TimerCallback callback = new TimerCallback(Update);
      timer = new Timer(callback, null, 50, 50);

      FileManager = new BufferIO(true);

      byte[] fileBuffer = default(byte[]);
      using (StreamReader sr = new StreamReader(Assets.Open("countdown.ogg")))
      {
        using (var memstream = new MemoryStream())
        {
          sr.BaseStream.CopyTo(memstream);
          fileBuffer = memstream.ToArray();
        }
      }

      FileManager.AddBuffer("file1", fileBuffer, fileBuffer.Length);
      FileManager.SetActive(true);

      sound = new sound();
      sound.create("file1");

      // Get our button from the layout resource,
      // and attach an event to it
      Button startbutton = FindViewById<Button>(Resource.Id.startButton);
      startbutton.Click += delegate {
        sound.play();
        Yse.Yse.System().AudioTest(true);
        startbutton.Text = "" + sound.length();
      };

      Button pausebutton = FindViewById<Button>(Resource.Id.pauseButton);
      pausebutton.Click += delegate { sound.pause(); };

      Button stopbutton = FindViewById<Button>(Resource.Id.stopButton);
      stopbutton.Click += delegate {
        sound.stop();
        Yse.Yse.System().AudioTest(false);
      };
    }

    static void Update(object state)
    {
      Yse.Yse.System().update();
    }

    protected override void OnDestroy()
    {
      base.OnDestroy();
      timer.Dispose();

      Yse.Yse.System().Dispose();
    }
  }
}

