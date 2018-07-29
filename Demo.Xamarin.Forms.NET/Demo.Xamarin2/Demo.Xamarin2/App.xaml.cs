using System;
using Xamarin.Forms;
using Xamarin.Forms.Xaml;

[assembly: XamlCompilation (XamlCompilationOptions.Compile)]
namespace Demo.Xamarin2
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
			// Handle when your app starts
		}

		protected override void OnSleep ()
		{
			// Handle when your app sleeps
		}

		protected override void OnResume ()
		{
			// Handle when your app resumes
		}
	}
}
