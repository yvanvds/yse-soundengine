using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xamarin.Forms;

namespace Demo.Xamarin.Forms
{
	public partial class MainPage : ContentPage
	{
		public MainPage()
		{
			InitializeComponent();
		}

    private void BasicsButton_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new BasicsDemo());
      
    }

    private void DSPButton_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new DspDemo());
    }
	}
}
