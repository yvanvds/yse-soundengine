using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

using Xamarin.Forms;

namespace Demo.Xamarin.Forms
{
	public partial class App : Application
	{
    bool active = false;

		public App ()
		{
			InitializeComponent();

      MainPage = new NavigationPage(new MainPage())
      {
        BarBackgroundColor = Color.Black,
        BarTextColor = Color.White,
      };
      NavigationPage.SetHasNavigationBar(MainPage, false);
		}


		protected override void OnStart ()
		{
      Global.Yse.System.Init();
      active = true;
      TimeSpan time = new TimeSpan(0, 0, 0, 0, 50); // 50 milliseconds interval
      Device.StartTimer(time, UpdateCallback);
    }

		protected override void OnSleep ()
		{
      active = false;
      Global.Yse.System.Close();
		}

		protected override void OnResume ()
		{
      Global.Yse.System.Init();
      active = true;
      TimeSpan time = new TimeSpan(0, 0, 0, 0, 50); // 50 milliseconds interval
      Device.StartTimer(time, UpdateCallback);
    }

    bool UpdateCallback()
    {
      if (!active) return false;
      Global.Yse.System.Update();
      return true;
    }
	}
}
