using Demo.Xamarin.Forms.Resources;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Xamarin.Forms;
using Xamarin.Forms.Xaml;

namespace Demo.Xamarin.Forms.Demos
{
	[XamlCompilation(XamlCompilationOptions.Compile)]
	public partial class BasicAudioTest : ContentPage
	{
		public BasicAudioTest ()
		{
			InitializeComponent ();

      LabelIntro.Text = AppResources.BasicsAudioTestIntro;
      LabelSwitch.Text = AppResources.BasicsAudioTestCheckbox;
		}

    private void SwitchToggled(object sender, EventArgs e)
    {
      Global.Yse.System.AudioTest = (sender as Switch).IsToggled;
    }

    protected override void OnDisappearing()
    {
      Global.Yse.System.AudioTest = false;
      base.OnDisappearing();
    }
  }
}