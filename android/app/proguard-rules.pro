# Add project specific ProGuard rules here.
-keepclassmembers class * {
    @kotlinx.serialization.SerialName <fields>;
}
-keepattributes *Annotation*,InnerClasses,Signature
