
using Demo.Xamarin.Forms.Resources;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Xamarin.Forms;
using Xamarin.Forms.Xaml;
using SkiaSharp;
using SkiaSharp.Views.Forms;

namespace Demo.Xamarin.Forms.Demos
{
  [XamlCompilation(XamlCompilationOptions.Compile)]
  public partial class Basic3D : ContentPage
  {
    SKPaint greenPaint = new SKPaint
    {
      Style = SKPaintStyle.Fill,
      Color = SKColors.Green
    };

    SKPaint bluePaint = new SKPaint
    {
      Style = SKPaintStyle.Fill,
      Color = SKColors.Blue
    };

    SKPoint center = new SKPoint();
    IYse.Pos listenerPos = new IYse.Pos();
    IYse.Pos sound1Pos = new IYse.Pos();
    IYse.Pos sound2Pos = new IYse.Pos();

    int target = -1;

    IYse.ISound sound1 = Global.Yse.NewSound();
    IYse.ISound sound2 = Global.Yse.NewSound();

    

    public Basic3D()
    {
      InitializeComponent();

      Info.Text = AppResources.Basics3DInfo;

      listenerPos.Set(0f, 0f, 0f);
      Global.Yse.Listener.Pos(listenerPos / 10f);

      sound1.Create("contact", null, true);
      sound1Pos.Set(-100f, 30f, 0f);
      sound1.SetPos(sound1Pos / 10f);
      sound1.Play();

      sound2.Create("drone", null, true);
      sound2Pos.Set(100f, -10f, 0f);
      sound2.SetPos(sound2Pos / 10f);
      sound2.Play();
    }

    protected override void OnDisappearing()
    {
      sound1.Dispose();
      sound2.Dispose();
      base.OnDisappearing();
    }

    private void PaintSurface(object sender, SKPaintSurfaceEventArgs e)
    {
      SKSurface surface = e.Surface;
      SKCanvas canvas = surface.Canvas;

      canvas.Clear(SKColors.DimGray);

      center.X = e.Info.Width / 2;
      center.Y = e.Info.Height / 2;

      // listener
      canvas.DrawCircle(center.X + listenerPos.X, center.Y + listenerPos.Y, 80, greenPaint);

      // sounds
      canvas.DrawCircle(center.X + sound1Pos.X, center.Y + sound1Pos.Y, 50, bluePaint);
      canvas.DrawCircle(center.X + sound2Pos.X, center.Y + sound2Pos.Y, 50, bluePaint);
    }

    private void OnTouch(object sender, SKTouchEventArgs e)
    {
      switch (e.ActionType)
      {
        case SKTouchAction.Pressed:
          SetNearestTarget(e.Location);
          e.Handled = true;
          break;
        case SKTouchAction.Moved:
          switch(target)
          {
            case 0:
              listenerPos.X = e.Location.X - center.X;
              listenerPos.Y = e.Location.Y - center.Y;
              Global.Yse.Listener.Pos(listenerPos / 10f);
              break;
            case 1:
              sound1Pos.X = e.Location.X - center.X;
              sound1Pos.Y = e.Location.Y - center.Y;
              sound1.SetPos(sound1Pos / 10f);
              break;
            case 2:
              sound2Pos.X = e.Location.X - center.X;
              sound2Pos.Y = e.Location.Y - center.Y;
              sound2.SetPos(sound2Pos / 10f);
              break;
          }
          canvasView.InvalidateSurface();
          e.Handled = true;
          break;
        case SKTouchAction.Released:
          target = -1;
          e.Handled = true;
          break;
      }
    }

    private void SetNearestTarget(SKPoint location)
    {
      target = -1;

      float distance = 10000;
      float d = GetDistance(listenerPos, location);
      if (d < distance && d < 50)
      {
        distance = d;
        target = 0;
      }

      d = GetDistance(sound1Pos, location);
      if(d < distance && d < 50)
      {
        distance = d;
        target = 1;
      }

      d = GetDistance(sound2Pos, location);
      if (d < distance && d < 50)
      {
        distance = d;
        target = 2;
      }
    }

    private float GetDistance(IYse.Pos pos, SKPoint target)
    {
      IYse.Pos t = new IYse.Pos(target.X - center.X, target.Y - center.Y, 0);
      //float X = target.X - center.X;
      //float Y = target.Y - center.Y;
      //float dX = Math.Abs(pos.X - X);
      //float dY = Math.Abs(pos.Y - Y);
      //return Math.Max(dX, dY);
      return IYse.Pos.Dist(pos, t);
    }
  }

}