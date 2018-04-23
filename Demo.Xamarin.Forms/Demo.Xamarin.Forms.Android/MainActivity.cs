using System;

using Android.App;
using Android.Content.PM;
using Android.Runtime;
using Android.Views;
using Android.Widget;
using Android.OS;
using Demo.Xamarin.Forms;
using YSE;
using System.IO;

namespace Demo.Xamarin.Forms.Droid
{
  [Activity(Label = "Demo.Xamarin.Forms", Icon = "@drawable/icon", Theme = "@style/MainTheme", MainLauncher = true, ConfigurationChanges = ConfigChanges.ScreenSize | ConfigChanges.Orientation)]
  public class MainActivity : global::Xamarin.Forms.Platform.Android.FormsAppCompatActivity
  {

    protected override void OnCreate(Bundle bundle)
    {
      TabLayoutResource = Resource.Layout.Tabbar;
      ToolbarResource = Resource.Layout.Toolbar;

      base.OnCreate(bundle);

      // This is the only line needed to active YSE in a native project
      Global.Yse = new YSE.YseInterface(OnLogMessage);
      // ... except when you need to play audio from asset files, which are a 
      // nuisance right now. We cannot access assets from the native libraries
      // because we'd need to pass the JNIenv, which cannot be done from Xamarin
      // as far as I know. And on top of that, we cannot access assets from
      // the PCL project either. For now I settle for this option although it surely
      // needs improving.
      LoadAssetBuffers();

      global::Xamarin.Forms.Forms.Init(this, bundle);
      LoadApplication(new Demo.Xamarin.Forms.App());
    }

    private void OnLogMessage(string message)
    {
      Android.Util.Log.Info("YSE", message);
    }

    private void LoadAssetBuffers()
    {
      YSE.BufferIO IO = new YSE.BufferIO();

      LoadBuffer(IO, "contact");
      LoadBuffer(IO, "countdown");
      LoadBuffer(IO, "drone");
      LoadBuffer(IO, "flies");
      LoadBuffer(IO, "g");
      LoadBuffer(IO, "kick");
      LoadBuffer(IO, "my2chords");
      LoadBuffer(IO, "pulse1");
      LoadBuffer(IO, "snare");

      IO.Active = true;
    }

    private void LoadBuffer(BufferIO IO, String fileName)
    {
      byte[] fileBuffer = default(byte[]);
      using (StreamReader sr = new StreamReader(Assets.Open(fileName + ".ogg")))
      {
        using (var memstream = new MemoryStream())
        {
          sr.BaseStream.CopyTo(memstream);
          fileBuffer = memstream.ToArray();
          IO.AddBuffer(fileName, fileBuffer, fileBuffer.Length);
        }
      }
    }
  }
}

