using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Xamarin.Forms;
using Xamarin.Forms.Xaml;

namespace Demo.Xamarin.Forms
{
  [XamlCompilation(XamlCompilationOptions.Compile)]
  public partial class DspDemo : ContentPage
  {
    public DspDemo()
    {
      InitializeComponent();
    }

    private void SimplePatcher_Clicked(object sender, EventArgs e)
    {
      Navigation.PushAsync(new Demos.DSPSimplePatcher());
    }
  }


}