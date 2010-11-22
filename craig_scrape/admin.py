from django.contrib import admin
from craig_scrape.models import post
from settings_local import EMAIL_HOST_USER, EMAIL_BODY
# favour django-mailer but fall back to django.core.mail
from django.conf import settings
 
if "mailer" in settings.INSTALLED_APPS:
    from mailer import send_mail
else:
    from django.core.mail import send_mail  


  
class postAdmin(admin.ModelAdmin):
    search_fields  = ('title',)
    list_display = ('title', 'email', 'active',)
    list_filter = ('active','creation_date','sites',)
    actions = ['make_active','make_inactive','email_users']

    def make_inactive(self,request, queryset):
    
        rows_updated = queryset.update(active=False)
    
        if rows_updated == 1:
            message_bit = "1 item was"
        else:
            message_bit = "%s items were" % rows_updated
        self.message_user(request, "%s successfully marked as inactive." % message_bit)
    
    make_inactive.short_description = "Mark selected as inactive"


    def make_active(self,request, queryset):
   
        rows_updated = queryset.update(active=True)
    
        if rows_updated == 1:
            message_bit = "1 item was"
        else:
            message_bit = "%s items were" % rows_updated
        self.message_user(request, "%s successfully marked as active." % message_bit)
    
    make_active.short_description = "Mark selected as active"

    def email_users(self,request, queryset):

        for entry in queryset:
            

            
            subject = 'RE: ' + entry.title
            message_body = EMAIL_BODY
            email = entry.email
        
            send_mail(subject, message_body, EMAIL_HOST_USER, [email])



        try:
            rows_updated = queryset.update(active=True)
        except:
            pass
            
        self.message_user(request, "bayors")
    
    email_users.short_description = "Add selected to email que"




admin.site.register(post,postAdmin)


