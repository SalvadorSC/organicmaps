package app.organicmaps.sdk.metro_live;

/** One estimated metro train. Position is not GPS. */
public final class MetroTrain
{
  public String mLine;
  public String mColor;
  public String mDestination;
  public String mNextStop;
  public String mKey;
  public double mLat;
  public double mLon;
  /** Clockwise degrees from north. 0 is north. */
  public double mHeadingDeg;
}
