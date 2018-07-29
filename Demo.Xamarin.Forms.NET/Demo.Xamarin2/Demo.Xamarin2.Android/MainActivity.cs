using System;

using Android.App;
using Android.Content.PM;
using Android.Runtime;
using Android.Views;
using Android.Widget;
using Android.OS;
using YSE;

namespace Demo.Xamarin2.Droid
{
	[Activity(Label = "Demo.Xamarin2", Icon = "@mipmap/icon", Theme = "@style/MainTheme", MainLauncher = true, ConfigurationChanges = ConfigChanges.ScreenSize | ConfigChanges.Orientation)]
	public class MainActivity : global::Xamarin.Forms.Platform.Android.FormsAppCompatActivity
	{
		protected override void OnCreate(Bundle bundle)
		{
			TabLayoutResource = Resource.Layout.Tabbar;
			ToolbarResource = Resource.Layout.Toolbar;

			base.OnCreate(bundle);

			Global.Yse = new YseInterface(OnLogMessage);

			global::Xamarin.Forms.Forms.Init(this, bundle);
			LoadApplication(new App());
		}

		private void OnLogMessage(string message)
		{
			//Android.Util.Log.Info("YSE", message);
		}

	}
}

