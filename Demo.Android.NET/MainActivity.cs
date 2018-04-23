using Android.App;
using Android.Widget;
using Android.OS;
using IYse;
using System.Threading;
using System.IO;

namespace Demo.Android.NET
{
  [Activity(Label = "Demo.Android.NET", MainLauncher = true, Icon = "@mipmap/icon")]
  public class MainActivity : Activity
  {
    ISound sound = null;
    Timer timer = null;
    YSE.BufferIO FileManager = null;
    static IYseInterface Y;

    protected override void OnCreate(Bundle savedInstanceState)
    {
      base.OnCreate(savedInstanceState);

      // Set our view from the "main" layout resource
      SetContentView(Resource.Layout.Main);

      //Yse.Yse.System().init();
      //Global = new YSE.Global(Yse.Yse.System());
      Y = new YSE.YseInterface(OnMessage);
      Y.System.Init();

      FileManager = Y.BufferIO as YSE.BufferIO;

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
      FileManager.Active = true;

      sound = new YSE.Sound();
      sound.Create("file1");

      TimerCallback callback = new TimerCallback(Update);
      timer = new Timer(callback, null, 50, 50);

      // Get our button from the layout resource,
      // and attach an event to it
      Button startbutton = FindViewById<Button>(Resource.Id.startButton);
      startbutton.Click += delegate {
        sound.Play();
        Y.System.AudioTest = true;
        startbutton.Text = "" + sound.Length;
      };

      Button pausebutton = FindViewById<Button>(Resource.Id.pauseButton);
      pausebutton.Click += delegate { sound.Pause(); };

      Button stopbutton = FindViewById<Button>(Resource.Id.stopButton);
      stopbutton.Click += delegate {
        sound.Stop();
        Y.System.AudioTest = false;
      };
    }

    void OnMessage(string message)
    {
      // implement
    }

    static void Update(object state)
    {
      Y.System.Update();
    }

    protected override void OnDestroy()
    {
      base.OnDestroy();
      timer.Dispose();

      Y.System.Close();
    }
  }
}

